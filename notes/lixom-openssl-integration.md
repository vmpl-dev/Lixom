# Lixom 对 OpenSSL 的改动和封装分析

**日期**: 2026-01-01
**相关文件**:
- `openssl-provider/src/xom_provider.c` - OpenSSL Provider 主文件
- `openssl-provider/src/xom_aes_128_ctr.c` - AES-128-CTR 实现
- `openssl-provider/src/xom_aes_128_gcm.c` - AES-128-GCM 实现
- `openssl-provider/src/xom_hmac_sha256.c` - HMAC-SHA256 实现
- `openssl-provider/src/xom_aes_common.c` - AES 通用函数
- `openssl-provider/src/xom_subpage_pool.cpp` - 子页池管理
- `openssl-provider/src/aes_aesni.s` - AES-NI 汇编实现
- `openssl-provider/src/aes_vaes.s` - VAES 汇编实现
- `openssl-provider/src/hmac_sha256.s` - HMAC-SHA256 汇编实现
- `openssl-provider/src/ghash.s` - GHASH 汇编实现

## 概述

Lixom **并未直接修改 OpenSSL 源码**，而是通过 **OpenSSL Provider 机制**（OpenSSL 3.x 引入）实现了一个自定义的加密算法提供者。这个 Provider 将加密算法的关键代码（特别是包含密钥的代码）放入 XOM 保护的内存区域，从而保护加密密钥不被读取。

### 核心设计理念

1. **不修改 OpenSSL**: 通过 Provider 机制扩展 OpenSSL，无需修改 OpenSSL 源码
2. **XOM 保护关键代码**: 将包含密钥的加密函数放入 XOM 内存
3. **密钥嵌入代码**: 密钥直接嵌入到 XOM 保护的汇编代码中
4. **子页池管理**: 使用子页池高效管理 XOM 内存

## 一、OpenSSL Provider 架构

### 1.1 Provider 机制

OpenSSL 3.x 引入了 Provider 机制，允许动态加载加密算法实现：

- **默认 Provider**: OpenSSL 内置的算法实现
- **自定义 Provider**: 第三方实现的算法（如 Lixom）

**Provider 初始化** (`xen/xen/openssl-provider/src/xom_provider.c:204`):

```204:281:xen/xen/openssl-provider/src/xom_provider.c
extern int
OSSL_provider_init(const OSSL_CORE_HANDLE *handle, const OSSL_DISPATCH *in, const OSSL_DISPATCH **out, void **provctx) {
    unsigned i, j;
    int no_cache = 1;
    size_t algo_count;
    const OSSL_ALGORITHM *algo;
    xom_provctx *local_provctx = calloc(1, sizeof(*local_provctx));
    OSSL_ALGORITHM hmac_entry = {
            "HMAC",
            "provider=xom",
            ossl_hmac_functions,
            "Wrapper for XOM-protected HMAC/SHA256"
    };
    const static OSSL_ALGORITHM aes_128_gcm_entry = {
            "AES-128-GCM:id-aes128-GCM:2.16.840.1.101.3.4.1.6",
            "provider=" PROVIDER_NAME,
            aes_128_gcm_functions,
            "XOM-protected implementation of AES-128-GCM"
    };
    const static OSSL_ALGORITHM aes_128_ctr_entry ={
            "AES-128-CTR",
            "provider=" PROVIDER_NAME,
            aes_128_ctr_functions,
            "XOM-protected implementation of AES-128-CTR"
    };

    memset(default_algorithms, 0, sizeof(default_algorithms));
    process_environment_vars();

    local_provctx->dflt_provider = OSSL_PROVIDER_load(NULL, "default");
    local_provctx->dflt_hmac = EVP_MAC_fetch(NULL, "HMAC", "provider=default");

    discover_hardware_support(local_provctx);

    // Simply re-export algorithms from the default provider for compatibility, but override
    // entries that we implement ourselves
    for (i = 0; i < countof(opcodes); i++) {
        algo = OSSL_PROVIDER_query_operation(local_provctx->dflt_provider, (int) opcodes[i], &no_cache);
        if (!algo) {
            default_algorithms[opcodes[i]] = empty_algo;
            continue;
        }

        algo_count = count_algos(algo);
        default_algorithms[opcodes[i]] = calloc(algo_count, sizeof(*default_algorithms[0]));
        memcpy(default_algorithms[opcodes[i]], algo, algo_count * sizeof(*default_algorithms[0]));


        for (j = 0; j < algo_count - 1; j++) {
            default_algorithms[opcodes[i]][j].property_definition = "provider=" PROVIDER_NAME;
            if(opcodes[i] == OSSL_OP_CIPHER) {
                if(strstr(default_algorithms[opcodes[i]][j].algorithm_names, "AES-128-GCM") != NULL)
                    default_algorithms[opcodes[i]][j] = aes_128_gcm_entry;
                else if(strstr(default_algorithms[opcodes[i]][j].algorithm_names, "AES-128-CTR") != NULL)
                    default_algorithms[opcodes[i]][j] = aes_128_ctr_entry;
            }
            if(!local_provctx->has_sha)
                continue;
            if(opcodes[i] == OSSL_OP_MAC) {
                if(strstr(default_algorithms[opcodes[i]][j].algorithm_names, "HMAC") != NULL)
                    default_algorithms[opcodes[i]][j] = hmac_entry;
            }
        }

    }

    EVP_set_default_properties(NULL, "provider=xom");
    printf("If you can read this, the XOM provider was successfully initialized!\n");
    printf("Using %s-based implementation!\n", local_provctx->has_vaes ? "VAES" : "AES-NI");
    if(!local_provctx->has_sha)
        printf("SHA instructions are not supported - not exporting HMAC implementation!\n");
    check_xom_mode();

    *out = xom_dispatch_table;
    *provctx = local_provctx;

    return 1;
}
```

**关键步骤**:
1. **加载默认 Provider**: 加载 OpenSSL 默认 Provider
2. **硬件检测**: 检测 CPU 特性（VAES、VPCLMULQDQ、SHA）
3. **算法覆盖**: 用 XOM 保护的实现覆盖默认算法
4. **设置默认**: 将 XOM Provider 设置为默认 Provider

### 1.2 Provider 上下文

**Provider 上下文结构** (`openssl-provider/include/aes_xom.h:52`):

```52:59:openssl-provider/include/aes_xom.h
struct {
    OSSL_PROVIDER *dflt_provider;
    void *_padding;
    EVP_MAC *dflt_hmac;
    unsigned char has_vaes;
    unsigned char has_vpclmulqdq;
    unsigned char has_sha;
} typedef xom_provctx;
```

**字段说明**:
- **`dflt_provider`**: 默认 Provider 引用（用于回退）
- **`dflt_hmac`**: 默认 HMAC 实现（用于回退）
- **`has_vaes`**: 是否支持 VAES 指令集
- **`has_vpclmulqdq`**: 是否支持 VPCLMULQDQ 指令集
- **`has_sha`**: 是否支持 SHA-NI 指令集

### 1.3 硬件支持检测

**硬件检测** (`xen/xen/openssl-provider/src/xom_provider.c:171`):

```171:178:xen/xen/openssl-provider/src/xom_provider.c
static void discover_hardware_support(xom_provctx* ctx) {
    size_t a, b, c, d;

    __cpuid_count(0x7, 0, a, b, c, d);
    ctx->has_vaes = (c >> 9) & 1;
    ctx->has_vpclmulqdq = (c >> 10) & 1;
    ctx->has_sha = xom_hmac_disabled ? 0 : ((b >> 29) & 1);
}
```

**检测的 CPU 特性**:
- **VAES**: 向量化 AES 指令（Intel Ice Lake+）
- **VPCLMULQDQ**: 向量化 PCLMULQDQ 指令（用于 GCM）
- **SHA-NI**: SHA 指令集（用于 HMAC-SHA256）

## 二、AES-128-CTR 实现

### 2.1 上下文结构

**AES-CTR 上下文** (`openssl-provider/src/xom_aes_128_ctr.c:10`):

```10:24:openssl-provider/src/xom_aes_128_ctr.c
struct {
    unsigned char __attribute__((aligned(32))) oiv[AES_128_CTR_IV_SIZE];
    unsigned char __attribute__((aligned(32))) iv[AES_128_CTR_IV_SIZE];
    unsigned char __attribute__((aligned(32))) block[AES_128_CTR_BLOCK_SIZE];
    unsigned int *refcount;
    unsigned char block_offset;
    void *aes_fun;
    unsigned int num;
    union {
        unsigned int d;
        unsigned char b[sizeof(unsigned int)];
    } ctr;
    unsigned char has_vaes : 1;
    unsigned char marked : 1;
} typedef xom_aes_ctr_context;
```

**关键字段**:
- **`aes_fun`**: 指向 XOM 保护的 AES 函数指针
- **`refcount`**: 引用计数（用于上下文复制）
- **`has_vaes`**: 是否使用 VAES 实现

### 2.2 初始化流程

**初始化函数** (`openssl-provider/src/xom_aes_128_ctr.c:81`):

```81:114:openssl-provider/src/xom_aes_128_ctr.c
int aes_128_ctr_init(void *vctx, const unsigned char *key, size_t keylen, const unsigned char *iv, size_t ivlen,
                     const OSSL_PARAM __attribute__((unused)) params[]) {
    const size_t vaes_size = (unsigned char*)aes_vaes_gctr_linear_end - (unsigned char*)aes_vaes_gctr_linear;
    const size_t aesni_size = (unsigned char*)aes_aesni_gctr_linear_end - (unsigned char*)aes_aesni_gctr_linear;
    xom_aes_ctr_context *ctx = vctx;
    unsigned i;
    unsigned char* staging_buffer = aligned_alloc(SUBPAGE_SIZE,SUBPAGE_SIZE * (max(vaes_size, aesni_size) / SUBPAGE_SIZE + 1));

    if (keylen != AES_128_CTR_KEY_SIZE)
        return 0;
    if (ivlen != AES_128_CTR_IV_SIZE)
        return 0;

    memcpy(ctx->oiv, iv, AES_128_CTR_IV_SIZE);
    memcpy(ctx->iv, iv, AES_128_CTR_IV_SIZE);

    if(ctx->has_vaes)
        memcpy(staging_buffer, aes_vaes_gctr_linear, vaes_size);
    else
        memcpy(staging_buffer, aes_aesni_gctr_linear, aesni_size);

    (ctx->has_vaes ? setup_vaes_128_key : setup_aesni_128_key)(staging_buffer, key);

    for (i = 0; i < sizeof(ctx->ctr.d); i++)
        ctx->ctr.b[i] = ctx->iv[(sizeof(ctx->iv) - 1) - i];

    ctx->aes_fun = subpage_pool_lock_into_xom(staging_buffer, ctx->has_vaes ? vaes_size : aesni_size);

    free(staging_buffer);
    if(!ctx->aes_fun)
        return 0;

    return 1;
}
```

**关键步骤**:
1. **分配暂存缓冲区**: 分配对齐的暂存缓冲区
2. **复制汇编代码**: 从汇编符号复制 AES 函数代码
3. **嵌入密钥**: 调用 `setup_vaes_128_key()` 或 `setup_aesni_128_key()` 将密钥嵌入代码
4. **锁定到 XOM**: 使用 `subpage_pool_lock_into_xom()` 将代码锁定到 XOM 内存
5. **保存函数指针**: 保存 XOM 保护的函数指针

### 2.3 密钥嵌入机制

**密钥设置函数** (`openssl-provider/src/xom_aes_common.c:4`):

```4:18:openssl-provider/src/xom_aes_common.c
void setup_aesni_128_key(unsigned char* dest, const unsigned char *key) {
    const size_t key_offset_lo = &aes_aesni_key_lo - (unsigned char *) aes_aesni_gctr_linear + MOV_OPCODE_SIZE;
    const size_t key_offset_hi = &aes_aesni_key_hi - (unsigned char *) aes_aesni_gctr_linear + MOV_OPCODE_SIZE;

    memcpy(dest + key_offset_lo, key, AES_128_CTR_KEY_SIZE >> 1);
    memcpy(dest + key_offset_hi, key + (AES_128_CTR_KEY_SIZE >> 1), AES_128_CTR_KEY_SIZE >> 1);
}

void setup_vaes_128_key(unsigned char* dest, const unsigned char *key) {
    const size_t key_offset_lo = &aes_vaes_key_lo - (unsigned char *) aes_vaes_gctr_linear + MOV_OPCODE_SIZE;
    const size_t key_offset_hi = &aes_vaes_key_hi - (unsigned char *) aes_vaes_gctr_linear + MOV_OPCODE_SIZE;

    memcpy(dest + key_offset_lo, key, AES_128_CTR_KEY_SIZE >> 1);
    memcpy(dest + key_offset_hi, key + (AES_128_CTR_KEY_SIZE >> 1), AES_128_CTR_KEY_SIZE >> 1);
}
```

**工作原理**:
- **汇编符号**: 汇编代码中定义了密钥位置符号（`aes_aesni_key_lo`, `aes_aesni_key_hi`）
- **偏移计算**: 计算密钥在代码中的偏移位置
- **密钥嵌入**: 将密钥直接写入代码中的密钥位置
- **MOV 指令**: 密钥嵌入在 `MOV imm64, reg` 指令的立即数中

### 2.4 加密/解密函数

**核心加密函数** (`openssl-provider/src/xom_aes_128_ctr.c:209`):

```209:247:openssl-provider/src/xom_aes_128_ctr.c
int
aes_128_ctr_cipher(void *vctx, unsigned char *out, size_t *outl, size_t outsize, const unsigned char *in, size_t inl) {
    xom_aes_ctr_context *ctx = vctx;
    unsigned num_in_blocks = inl / AES_128_CTR_BLOCK_SIZE, num_out_blocks =
            outsize / AES_128_CTR_BLOCK_SIZE, blocks_processed, i;
    make_aligned(32, out, num_in_blocks * AES_128_CTR_BLOCK_SIZE)
    make_const_aligned(32, in, num_in_blocks * AES_128_CTR_BLOCK_SIZE)

    if (!outl)
        return 0;
    *outl = 0;

    if (num_in_blocks > num_out_blocks)
        num_in_blocks = num_out_blocks;
    num_out_blocks = num_in_blocks;

    while (num_in_blocks) {
        blocks_processed = num_in_blocks - call_aes_implementation(
                ctx->iv,
                in + ((num_out_blocks - num_in_blocks) * AES_128_CTR_BLOCK_SIZE),
                out + ((num_out_blocks - num_in_blocks) * AES_128_CTR_BLOCK_SIZE),
                num_in_blocks,
                ctx->aes_fun,
                ctx->has_vaes
        );
        if (blocks_processed > num_in_blocks)
            break;
        num_in_blocks -= blocks_processed;
        ctx->ctr.d += blocks_processed;
        for (i = 0; i < sizeof(ctx->ctr.d); i++)
            ctx->iv[(sizeof(ctx->iv) - 1) - i] = ctx->ctr.b[i];
    }

    free_aligned(out, num_out_blocks * AES_128_CTR_BLOCK_SIZE)
    free_const_aligned(in, num_out_blocks * AES_128_CTR_BLOCK_SIZE)
    *outl = num_out_blocks * AES_128_CTR_BLOCK_SIZE;

    return 1;
}
```

**关键操作**:
1. **对齐处理**: 确保输入/输出缓冲区对齐（AVX2 要求）
2. **调用 XOM 函数**: 通过 `call_aes_implementation()` 调用 XOM 保护的 AES 函数
3. **计数器更新**: 更新 CTR 计数器和 IV

**调用 XOM 函数** (`openssl-provider/include/aes_xom.h:89`):

```89:112:openssl-provider/include/aes_xom.h
static size_t __attribute__((optimize("O0")))
call_aesni_implementation(void *icb, const void *x, void *y, unsigned int num_blocks, const void *aes_fun) {
    size_t ret;
    asm volatile ("call *%1" : "=a" (ret) : "r"(aes_fun), "D"(icb), "S" (x), "d"(y), "c"(num_blocks) : "r14", "r15", "r8",
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
    "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
    return ret;
}

static size_t __attribute__((optimize("O0"), target("avx2")))
call_vaes_implementation(void *icb, const void *x, void *y, unsigned int num_blocks, const void *aes_fun) {
    size_t ret;
    asm volatile ("call *%1" : "=a" (ret) : "r"(aes_fun), "D"(icb), "S" (x), "d"(y), "c"(num_blocks)
            : "r14", "r15", "r8",
    "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "ymm8", "ymm9",
    "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15");
    return ret;
}

static inline size_t
call_aes_implementation(void *icb, const void *x, void *y, unsigned int num_blocks, const void *aes_fun,
                        unsigned char has_vaes) {
    return (has_vaes ? call_vaes_implementation : call_aesni_implementation)(icb, x, y, num_blocks, aes_fun);
}
```

**关键点**:
- **内联汇编**: 使用内联汇编直接调用 XOM 保护的函数
- **寄存器清理**: 在 clobber 列表中声明所有使用的寄存器
- **优化禁用**: 使用 `__attribute__((optimize("O0")))` 防止编译器优化

## 三、AES-128-GCM 实现

### 3.1 GCM 上下文结构

**GCM 上下文** (`openssl-provider/src/xom_aes_128_gcm.c:21`):

```21:49:openssl-provider/src/xom_aes_128_gcm.c
struct {
    unsigned char AVX_ALIGNED oiv[AES_128_GCM_IV_SIZE];
    unsigned char AVX_ALIGNED block[AES_128_GCM_BLOCK_SIZE];
    unsigned char AVX_ALIGNED tag[AES_128_GCM_TAG_SIZE];
    unsigned char AVX_ALIGNED J[AES_128_GCM_TAG_SIZE];
    __m128i AVX_ALIGNED H;
    __m128i AVX_ALIGNED hash_state;
    __m128i AVX_ALIGNED J0;
    union {
        unsigned int d;
        unsigned char b[sizeof(unsigned int)];
    } __attribute__((aligned(sizeof(unsigned int)))) ctr;
    void* Htable;
    unsigned char *aad;
    unsigned int *refcount;
    size_t ivlen;
    size_t aad_len;
    size_t num_ciphertext_blocks;
    unsigned char block_offset;
    void *aes_fun;
    unsigned char *staging_buffer;
    unsigned char decrypt: 1;
    unsigned char key_initialized: 1;
    unsigned char iv_initialized: 1;
    unsigned char first_update: 1;
    unsigned char has_h: 1;
    unsigned char has_vaes: 1;
    unsigned char has_vpclmulqdq : 1;
} typedef aes_128_gcm_context;
```

**关键字段**:
- **`aes_fun`**: XOM 保护的 AES 函数指针
- **`Htable`**: GHASH 查找表
- **`has_vpclmulqdq`**: 是否使用 VPCLMULQDQ 加速 GHASH

### 3.2 GCM 初始化

GCM 初始化类似 CTR，但还需要初始化 GHASH：

1. **AES 函数**: 将 AES 函数锁定到 XOM
2. **GHASH 表**: 初始化 GHASH 查找表
3. **H 值**: 计算 GCM 的 H 值（使用 AES 加密零块）

## 四、HMAC-SHA256 实现

### 4.1 HMAC 上下文结构

**HMAC 上下文** (`openssl-provider/src/xom_hmac_sha256.c:50`):

```50:66:openssl-provider/src/xom_hmac_sha256.c
struct {
    EVP_MAC_CTX* dflt_ctx;
    xom_provctx provctx;
    unsigned char* block;
    unsigned char* hash_state;
    unsigned int* refcount;
    unsigned int keylen;
    unsigned int bytes_compressed;
    struct xombuf* xbuf;
    hmac_fun hmac_fun;
    unsigned char block_offset;
    unsigned char use_passthrough : 1;
    unsigned char first_update : 1;
    unsigned char final_update : 1;
    unsigned char locked : 1;
    unsigned char marked : 1;
} typedef hmac_sha256_ctx;
```

**关键字段**:
- **`xbuf`**: XOM 缓冲区（包含 HMAC 函数）
- **`hmac_fun`**: XOM 保护的 HMAC 函数指针
- **`dflt_ctx`**: 默认 HMAC 上下文（用于回退）

### 4.2 HMAC 初始化

**HMAC 创建** (`openssl-provider/src/xom_hmac_sha256.c:113`):

```113:132:openssl-provider/src/xom_hmac_sha256.c
static void *hmac_new(void *provctx) {
    hmac_sha256_ctx* ret = calloc(1, sizeof(*ret));

    memset(ret, 0, sizeof(*ret));

    ret->refcount = calloc(1, sizeof(*(ret->refcount)));
    *(ret->refcount) = 1;
    memcpy(&ret->provctx, provctx, sizeof(ret->provctx));
    ret->dflt_ctx = EVP_MAC_CTX_new(ret->provctx.dflt_hmac);
    ret->block = aligned_alloc(AVX2_ALIGNMENT, HMAC_SHA256_BLOCK_SIZE * 4);
    ret->hash_state = aligned_alloc(AVX2_ALIGNMENT, HMAC_SHA256_MAC_SIZE + AVX2_ALIGNMENT * 2);
    ret->keylen = 64;
    ret->xbuf = xom_alloc(PAGE_SIZE);
    ret->hmac_fun = (void*)(*(unsigned char**)ret->xbuf + ((unsigned char*)hmac256 - (unsigned char*)hmac256_start));
    ret->use_passthrough = 1;

    xom_write(ret->xbuf, hmac256_start, ((unsigned char*)hmac256_end - (unsigned char*)hmac256_start), 0);

    return ret;
}
```

**关键步骤**:
1. **分配 XOM 缓冲区**: 使用 `xom_alloc()` 分配 XOM 内存
2. **写入 HMAC 代码**: 使用 `xom_write()` 将 HMAC 汇编代码写入 XOM 缓冲区
3. **计算函数指针**: 计算 HMAC 函数在 XOM 缓冲区中的地址

### 4.3 密钥设置

**密钥设置** (`openssl-provider/src/xom_hmac_sha256.c:83`):

```83:111:openssl-provider/src/xom_hmac_sha256.c
static void set_hmac_sha256_keys(hmac_sha256_ctx* ctx, const void* key, size_t key_len) {
    const void* fn_base = *(void**)ctx->xbuf;
    uint8_t* const hmac_key_ptrs[] = {
            keyptr(fn_base, quad0_key_lo), keyptr(fn_base, quad1_key_lo),
            keyptr(fn_base, quad2_key_lo), keyptr(fn_base, quad3_key_lo),
            keyptr(fn_base, quad0_key_hi), keyptr(fn_base, quad1_key_hi),
            keyptr(fn_base, quad2_key_hi), keyptr(fn_base, quad3_key_hi),
    };
    uint8_t key_buf[64];
    unsigned i, j;

    if (!key)
        return;

    if (ctx->locked)
        return;

    memset(key_buf, 0, sizeof(key_buf));
    memcpy(key_buf, key, min(sizeof(key_buf), key_len));

    for (i = 0; i < countof(hmac_key_ptrs); i++) {
        for (j = 0; j < sizeof(uint64_t); j++) {
            hmac_key_ptrs[i][j] = key_buf[sizeof(uint64_t) * i + j];
        }
    }

    while(!_rdrand64_step((unsigned long long*) keyptr(fn_base, hmac_memenc_key_lo)));
    while(!_rdrand64_step((unsigned long long*) keyptr(fn_base, hmac_memenc_key_hi)));
}
```

**关键操作**:
1. **密钥位置**: 计算密钥在 XOM 代码中的位置
2. **密钥嵌入**: 将密钥写入 XOM 代码中的密钥位置
3. **内存加密密钥**: 生成随机密钥用于内存加密（如果支持）

### 4.4 HMAC 锁定

**锁定到 XOM** (`openssl-provider/src/xom_hmac_sha256.c:174`):

```174:178:openssl-provider/src/xom_hmac_sha256.c
    xom_lock(ctx->xbuf);

    if(get_xom_mode() == XOM_MODE_SLAT && !ctx->marked) {
        if(xom_mark_register_clear(ctx->xbuf, 0, 0))
```

**关键步骤**:
1. **锁定缓冲区**: 使用 `xom_lock()` 锁定 XOM 缓冲区
2. **寄存器清除**: 如果使用 EPT 模式，标记寄存器清除

## 五、子页池管理

### 5.1 子页池设计

**目的**: 高效管理 XOM 内存，避免频繁分配/释放

**池结构** (`openssl-provider/src/xom_subpage_pool.cpp:14`):

```14:24:openssl-provider/src/xom_subpage_pool.cpp
struct subpage_list_entry {
    struct xom_subpages* subpages;
    size_t last_page_marked;
    size_t subpages_used;
    std::unordered_set<uintptr_t> buffers_used;

    subpage_list_entry() : subpages(nullptr), last_page_marked(0), subpages_used(0), buffers_used(std::unordered_set<uintptr_t>()) {}
    explicit subpage_list_entry(struct xom_subpages* subpages) : subpages(subpages), last_page_marked(0), subpages_used(0), buffers_used(std::unordered_set<uintptr_t>()) {}
};

static std::vector<subpage_list_entry> subpage_pool;
```

**字段说明**:
- **`subpages`**: 子页结构（由 libxom 管理）
- **`subpages_used`**: 已使用的子页数量
- **`buffers_used`**: 已使用的缓冲区集合

### 5.2 锁定到 XOM

**锁定函数** (`openssl-provider/src/xom_subpage_pool.cpp:37`):

```37:66:openssl-provider/src/xom_subpage_pool.cpp
extern "C" void* subpage_pool_lock_into_xom (const unsigned char* data, size_t size) {
    void* ret;
    struct xom_subpages *new_subpages;

    for (auto curr_entry = subpage_pool.rbegin(); curr_entry != subpage_pool.rend(); curr_entry++) {

        if((POOL_BUFFER_SIZE / SUBPAGE_SIZE) - curr_entry->subpages_used < bytes_to_subpages(size))
            continue;

        ret = xom_fill_and_lock_subpages(curr_entry->subpages, size, data);
        if (ret) {
            update_entry(*curr_entry, size, ret);
            return ret;
        }
    }

    new_subpages = xom_alloc_subpages(POOL_BUFFER_SIZE);
    if (!new_subpages)
        return nullptr;
    auto curr_entry = subpage_list_entry(new_subpages);
    ret = xom_fill_and_lock_subpages(curr_entry.subpages, size, data);
    if(ret) {
        update_entry(curr_entry, size, ret);
        subpage_pool.push_back(curr_entry);
        return ret;
    }

    xom_free_all_subpages(curr_entry.subpages);
    return nullptr;
}
```

**关键步骤**:
1. **查找可用池**: 从后往前查找有足够空间的子页池
2. **填充并锁定**: 使用 `xom_fill_and_lock_subpages()` 填充数据并锁定
3. **创建新池**: 如果没有可用池，创建新的子页池
4. **更新条目**: 更新池使用情况

### 5.3 释放机制

**释放函数** (`openssl-provider/src/xom_subpage_pool.cpp:68`):

```68:83:openssl-provider/src/xom_subpage_pool.cpp
extern "C" void subpage_pool_free(void* const data) {
    if(!data)
        return;

    auto it = std::find_if(subpage_pool.begin(), subpage_pool.end(),
                           [data] (const subpage_list_entry& e) -> bool {
        return e.buffers_used.find(reinterpret_cast<uintptr_t>(data)) == e.buffers_used.end();
    });

    if (it == subpage_pool.end())
        return;

    it->buffers_used.erase(reinterpret_cast<uintptr_t>(data));
    if (xom_free_subpages(it->subpages, data) == 1 && it->subpages_used >= POOL_FREE_THRESHOLD)
        subpage_pool.erase(it);
}
```

**关键操作**:
1. **查找缓冲区**: 查找包含该缓冲区的池条目
2. **释放子页**: 使用 `xom_free_subpages()` 释放子页
3. **清理池**: 如果池使用率超过阈值，清理整个池

## 六、汇编代码实现

### 6.1 AES-NI 实现

**文件**: `openssl-provider/src/aes_aesni.s`

**特点**:
- 使用 AES-NI 指令集
- 密钥嵌入在代码中（通过符号 `aes_aesni_key_lo`, `aes_aesni_key_hi`）
- 线性 GCTR 模式实现

### 6.2 VAES 实现

**文件**: `openssl-provider/src/aes_vaes.s`

**特点**:
- 使用 VAES 指令集（向量化 AES）
- 更高的性能（一次处理多个块）
- 密钥嵌入机制与 AES-NI 相同

### 6.3 HMAC-SHA256 实现

**文件**: `openssl-provider/src/hmac_sha256.s`

**特点**:
- 使用 SHA-NI 指令集
- 密钥嵌入在代码中
- 支持内存加密（如果 CPU 支持）

### 6.4 GHASH 实现

**文件**: `openssl-provider/src/ghash.s`

**特点**:
- 使用 PCLMULQDQ 或 VPCLMULQDQ 指令
- 用于 GCM 模式的认证

## 七、关键设计特点

### 7.1 密钥保护策略

**密钥嵌入代码**:
- 密钥直接嵌入到汇编代码中
- 通过符号定位密钥位置
- 密钥成为代码的一部分

**优势**:
- ✅ **密钥不可读**: 代码在 XOM 内存中，无法读取
- ✅ **密钥不可写**: XOM 内存只能执行，不能写入
- ✅ **密钥不可复制**: 无法通过内存转储获取密钥

### 7.2 函数调用机制

**间接调用**:
- 通过函数指针调用 XOM 保护的函数
- 使用内联汇编确保寄存器清理
- 禁用编译器优化防止意外泄露

**寄存器清理**:
- 在 clobber 列表中声明所有使用的寄存器
- 确保寄存器在调用前后被清理

### 7.3 内存管理

**子页池**:
- 使用子页池减少分配开销
- 支持多个缓冲区共享一个池
- 自动清理未使用的池

**对齐要求**:
- AVX2 要求 32 字节对齐
- 自动处理未对齐的缓冲区

### 7.4 硬件加速支持

**多级回退**:
1. **VAES + VPCLMULQDQ**: 最高性能（Ice Lake+）
2. **AES-NI + PCLMULQDQ**: 中等性能（Westmere+）
3. **软件实现**: 最低性能（回退到默认 Provider）

## 八、使用方式

### 8.1 配置方式

**OpenSSL 配置文件** (`openssl-provider/openssl.conf`):

```1:12:openssl-provider/openssl.conf
openssl_conf = openssl_init

[openssl_init]
providers = provider_section

[provider_section]
xom = xom_provider

[xom_provider]
activate = 1
module = ${ENV::XMODULE}
identity = xom
```

**环境变量**:
- **`XMODULE`**: XOM Provider 库路径
- **`LIBXOM_PROVIDER_DEBUG=1`**: 启用调试输出
- **`LIBXOM_PROVIDER_NO_HMAC=1`**: 禁用 HMAC 实现

### 8.2 编程方式

**加载 Provider** (`demos/demo_https.c`):

```c
OSSL_PROVIDER *xom_provider = OSSL_PROVIDER_load(NULL, "xom");
```

**使用算法**:
```c
EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, "AES-128-CTR", "provider=xom");
```

### 8.3 命令行方式

**使用 OpenSSL 命令行工具**:
```bash
OPENSSL_CONF=openssl.conf XMODULE=./libxom_provider.so \
openssl dgst -sha256 -hmac "secret_key" file.txt
```

## 九、与 OpenSSL 的集成点

### 9.1 Provider 接口

Lixom 实现了以下 OpenSSL Provider 接口：

**Cipher 接口**:
- `OSSL_FUNC_CIPHER_NEWCTX`
- `OSSL_FUNC_CIPHER_ENCRYPT_INIT` / `OSSL_FUNC_CIPHER_DECRYPT_INIT`
- `OSSL_FUNC_CIPHER_UPDATE`
- `OSSL_FUNC_CIPHER_FINAL`
- `OSSL_FUNC_CIPHER_CIPHER`

**MAC 接口**:
- `OSSL_FUNC_MAC_NEWCTX`
- `OSSL_FUNC_MAC_INIT`
- `OSSL_FUNC_MAC_UPDATE`
- `OSSL_FUNC_MAC_FINAL`

### 9.2 算法覆盖

Lixom Provider 覆盖以下算法：

- **AES-128-CTR**: 完全实现
- **AES-128-GCM**: 完全实现
- **HMAC-SHA256**: 完全实现
- **其他算法**: 回退到默认 Provider

### 9.3 兼容性

**向后兼容**:
- 其他算法使用默认 Provider
- 支持 OpenSSL 3.x API
- 不影响现有代码

## 十、安全特性

### 10.1 XOM 保护

**代码保护**:
- AES/HMAC 函数在 XOM 内存中
- 密钥嵌入在代码中
- 无法通过内存读取获取密钥

### 10.2 寄存器清除

**EPT 模式**:
- 支持寄存器清除机制
- 防止侧信道攻击
- 在中断/异常时清除寄存器

### 10.3 内存加密

**HMAC 支持**:
- 如果 CPU 支持，使用内存加密
- 随机生成加密密钥
- 保护中间状态

## 十一、性能优化

### 11.1 硬件加速

**VAES**:
- 向量化 AES，一次处理多个块
- 显著提高吞吐量

**VPCLMULQDQ**:
- 向量化 GHASH
- 加速 GCM 模式

### 11.2 子页池

**内存复用**:
- 减少分配/释放开销
- 提高内存利用率
- 减少碎片

### 11.3 对齐优化

**自动对齐**:
- 自动处理未对齐缓冲区
- 减少复制开销
- 提高性能

## 十二、总结

### 12.1 核心改动

1. **Provider 实现**: 实现了完整的 OpenSSL Provider
2. **XOM 集成**: 将关键代码放入 XOM 内存
3. **密钥嵌入**: 密钥直接嵌入汇编代码
4. **子页池**: 高效管理 XOM 内存

### 12.2 设计优势

1. **无需修改 OpenSSL**: 通过 Provider 机制扩展
2. **透明集成**: 应用程序无需修改
3. **硬件加速**: 充分利用 CPU 特性
4. **安全保护**: XOM 保护密钥和代码

### 12.3 支持的算法

- ✅ **AES-128-CTR**: 完全支持
- ✅ **AES-128-GCM**: 完全支持
- ✅ **HMAC-SHA256**: 完全支持
- ⚠️ **其他算法**: 回退到默认 Provider

### 12.4 使用场景

1. **HTTPS/TLS**: 保护 TLS 会话密钥
2. **数据加密**: 保护加密密钥
3. **消息认证**: 保护 HMAC 密钥
4. **密钥管理**: 保护密钥存储

## 十三、参考文档

- [Lixom EPT-XOM 实现](./lixom-ept-xom-implementation.md)
- [OpenSSL Provider 文档](https://www.openssl.org/docs/man3.0/man7/provider.html)
- [OpenSSL Provider 示例](https://github.com/openssl/openssl/tree/master/demos/provider)
