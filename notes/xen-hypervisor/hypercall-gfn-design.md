# Xen Hypercall 基于 GFN 的设计模式

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/arch/x86/traps.c` - Hypercall page 创建
- `xen/xen/arch/x86/guest/xen/hypercall_page.S` - Hypercall page 实现
- `xen/xen/include/xen/guest_access.h` - Guest 内存访问接口
- `xen/xen/arch/x86/hvm/viridian/viridian.c` - Viridian hypercall（使用 GPA）
- `xen/xen/arch/x86/hvm/hypercall.c` - HVM hypercall 处理

## 概述

Xen 的 hypercall 设计基于 **GFN (Guest Frame Number)**，即客户物理地址。这种设计模式与 **AMD SEV-ES 的 GHCB (Guest-Host Communication Block)** 协议有相似之处：两者都通过在物理页中存放客户请求来实现 Guest 与 Hypervisor 之间的通信。

**核心观点**: Xen hypercall 使用 GFN 作为地址空间，所有指向 guest 内存的指针都是基于 GFN 的，Hypervisor 通过 P2M（Physical-to-Machine）映射将这些 GFN 转换为 MFN（Machine Frame Number）来访问实际内存。

## 一、Hypercall 中的 GFN 使用

### 1.1 Hypercall Page 使用 GFN

**Hypercall Page** 是 Xen 在 Guest 内存中写入的 hypercall stub 页面，其地址本身就是 GFN。

**创建 Hypercall Page**:

```1019:1073:xen/xen/arch/x86/traps.c
int guest_wrmsr_xen(struct vcpu *v, uint32_t idx, uint64_t val)
{
    struct domain *d = v->domain;
    /* Optionally shift out of the way of Viridian architectural MSRs. */
    uint32_t base = is_viridian_domain(d) ? 0x40000200 : 0x40000000;

    switch ( idx - base )
    {
    case 0: /* Write hypercall page */
    {
        void *hypercall_page;
        unsigned long gmfn = val >> PAGE_SHIFT;
        unsigned int page_index = val & (PAGE_SIZE - 1);
        struct page_info *page;
        p2m_type_t t;

        if ( page_index > 0 )
        {
            gdprintk(XENLOG_WARNING,
                     "wrmsr hypercall page index %#x unsupported\n",
                     page_index);
            return X86EMUL_EXCEPTION;
        }

        page = get_page_from_gfn(d, gmfn, &t, P2M_ALLOC);

        if ( !page || !get_page_type(page, PGT_writable_page) )
        {
            if ( page )
                put_page(page);

            if ( p2m_is_paging(t) )
            {
                p2m_mem_paging_populate(d, _gfn(gmfn));
                return X86EMUL_RETRY;
            }

            gdprintk(XENLOG_WARNING,
                     "Bad GMFN %lx (MFN %#"PRI_mfn") to MSR %08x\n",
                     gmfn, mfn_x(page ? page_to_mfn(page) : INVALID_MFN), base);
            return X86EMUL_EXCEPTION;
        }

        hypercall_page = __map_domain_page(page);
        init_hypercall_page(d, hypercall_page);
        unmap_domain_page(hypercall_page);

        put_page_and_type(page);
        return X86EMUL_OKAY;
    }

    default:
        return X86EMUL_EXCEPTION;
    }
}
```

**关键步骤**:
1. **提取 GFN**: `gmfn = val >> PAGE_SHIFT` - 从 MSR 值中提取 GFN
2. **GFN 到 MFN 转换**: `get_page_from_gfn(d, gmfn, &t, P2M_ALLOC)` - 通过 P2M 映射获取 MFN
3. **映射到 Hypervisor**: `__map_domain_page(page)` - 将 MFN 映射到 Hypervisor 地址空间
4. **写入 Hypercall Stub**: `init_hypercall_page(d, hypercall_page)` - 写入 hypercall 代码

### 1.2 Hypercall Page 结构

**Hypercall Page 布局**:

```1:60:xen/xen/arch/x86/guest/xen/hypercall_page.S
#include <asm/page.h>
#include <asm/asm_defns.h>
#include <public/xen.h>

        .section ".text.page_aligned", "ax", @progbits
        .p2align PAGE_SHIFT

GLOBAL(hypercall_page)
         /* Poisoned with `ret` for safety before hypercalls are set up. */
        .fill PAGE_SIZE, 1, 0xc3
        .type hypercall_page, STT_OBJECT
        .size hypercall_page, PAGE_SIZE

/*
 * Identify a specific hypercall in the hypercall page
 * @param name Hypercall name.
 */
#define DECLARE_HYPERCALL(name)                                                 \
        .globl HYPERCALL_ ## name;                                              \
        .type  HYPERCALL_ ## name, STT_FUNC;                                    \
        .size  HYPERCALL_ ## name, 32;                                          \
        .set   HYPERCALL_ ## name, hypercall_page + __HYPERVISOR_ ## name * 32

DECLARE_HYPERCALL(set_trap_table)
DECLARE_HYPERCALL(mmu_update)
DECLARE_HYPERCALL(set_gdt)
DECLARE_HYPERCALL(stack_switch)
DECLARE_HYPERCALL(set_callbacks)
DECLARE_HYPERCALL(fpu_taskswitch)
DECLARE_HYPERCALL(sched_op_compat)
DECLARE_HYPERCALL(platform_op)
DECLARE_HYPERCALL(set_debugreg)
DECLARE_HYPERCALL(get_debugreg)
DECLARE_HYPERCALL(update_descriptor)
DECLARE_HYPERCALL(memory_op)
DECLARE_HYPERCALL(multicall)
DECLARE_HYPERCALL(update_va_mapping)
DECLARE_HYPERCALL(set_timer_op)
DECLARE_HYPERCALL(event_channel_op_compat)
DECLARE_HYPERCALL(xen_version)
DECLARE_HYPERCALL(console_io)
DECLARE_HYPERCALL(physdev_op_compat)
DECLARE_HYPERCALL(grant_table_op)
DECLARE_HYPERCALL(vm_assist)
DECLARE_HYPERCALL(update_va_mapping_otherdomain)
DECLARE_HYPERCALL(iret)
DECLARE_HYPERCALL(vcpu_op)
DECLARE_HYPERCALL(set_segment_base)
DECLARE_HYPERCALL(mmuext_op)
DECLARE_HYPERCALL(xsm_op)
DECLARE_HYPERCALL(nmi_op)
DECLARE_HYPERCALL(sched_op)
DECLARE_HYPERCALL(callback_op)
DECLARE_HYPERCALL(xenoprof_op)
DECLARE_HYPERCALL(event_channel_op)
DECLARE_HYPERCALL(physdev_op)
DECLARE_HYPERCALL(hvm_op)
DECLARE_HYPERCALL(sysctl)
DECLARE_HYPERCALL(domctl)
DECLARE_HYPERCALL(kexec_op)
DECLARE_HYPERCALL(argo_op)
DECLARE_HYPERCALL(xenpmu_op)

DECLARE_HYPERCALL(arch_0)
DECLARE_HYPERCALL(arch_1)
DECLARE_HYPERCALL(arch_2)
DECLARE_HYPERCALL(arch_3)
DECLARE_HYPERCALL(arch_4)
DECLARE_HYPERCALL(arch_5)
DECLARE_HYPERCALL(arch_6)
DECLARE_HYPERCALL(arch_7)
```

**特点**:
- 每个 hypercall stub 占用 32 字节
- Stub 位置: `hypercall_page + index * 32`
- Guest 通过 `call hypercall_page + index * 32` 调用

### 1.3 Guest 内存访问接口

**Guest Handle 机制**:

Xen 使用 `XEN_GUEST_HANDLE` 类型来表示指向 guest 内存的指针，这些指针在 guest 地址空间中是基于 GFN 的。

**定义**:

```15:40:xen/xen/include/public/arch-x86/xen.h
/* Structural guest handles introduced in 0x00030201. */
#if __XEN_INTERFACE_VERSION__ >= 0x00030201
#define ___DEFINE_XEN_GUEST_HANDLE(name, type) \
    typedef struct { type *p; } __guest_handle_ ## name
#else
#define ___DEFINE_XEN_GUEST_HANDLE(name, type) \
    typedef type * __guest_handle_ ## name
#endif

/*
 * XEN_GUEST_HANDLE represents a guest pointer, when passed as a field
 * in a struct in memory.
 * XEN_GUEST_HANDLE_PARAM represent a guest pointer, when passed as an
 * hypercall argument.
 * XEN_GUEST_HANDLE_PARAM and XEN_GUEST_HANDLE are the same on X86 but
 * they might not be on other architectures.
 */
#define __DEFINE_XEN_GUEST_HANDLE(name, type) \
    ___DEFINE_XEN_GUEST_HANDLE(name, type);   \
    ___DEFINE_XEN_GUEST_HANDLE(const_##name, const type)
#define DEFINE_XEN_GUEST_HANDLE(name)   __DEFINE_XEN_GUEST_HANDLE(name, name)
#define __XEN_GUEST_HANDLE(name)        __guest_handle_ ## name
#define XEN_GUEST_HANDLE(name)          __XEN_GUEST_HANDLE(name)
#define XEN_GUEST_HANDLE_PARAM(name)    XEN_GUEST_HANDLE(name)
#define set_xen_guest_handle_raw(hnd, val)  do { (hnd).p = val; } while (0)
#define set_xen_guest_handle(hnd, val) set_xen_guest_handle_raw(hnd, val)
```

**Guest 内存复制函数**:

```56:107:xen/xen/include/xen/guest_access.h
/*
 * Copy an array of objects to guest context via a guest handle,
 * specifying an offset into the guest array.
 */
#define copy_to_guest_offset(hnd, off, ptr, nr) ({      \
    const typeof(*(ptr)) *_s = (ptr);                   \
    char (*_d)[sizeof(*_s)] = (void *)(hnd).p;          \
    /* Check that the handle is not for a const type */ \
    void *__maybe_unused _t = (hnd).p;                  \
    (void)((hnd).p == _s);                              \
    raw_copy_to_guest(_d+(off), _s, sizeof(*_s)*(nr));  \
})

/*
 * Clear an array of objects in guest context via a guest handle,
 * specifying an offset into the guest array.
 */
#define clear_guest_offset(hnd, off, nr) ({    \
    void *_d = (hnd).p;                        \
    raw_clear_guest(_d+(off), nr);             \
})

/*
 * Copy an array of objects from guest context via a guest handle,
 * specifying an offset into the guest array.
 */
#define copy_from_guest_offset(ptr, hnd, off, nr) ({    \
    const typeof(*(ptr)) *_s = (hnd).p;                 \
    typeof(*(ptr)) *_d = (ptr);                         \
    raw_copy_from_guest(_d, _s+(off), sizeof(*_d)*(nr));\
})

/* Copy sub-field of a structure to guest context via a guest handle. */
#define copy_field_to_guest(hnd, ptr, field) ({         \
    const typeof(&(ptr)->field) _s = &(ptr)->field;     \
    void *_d = &(hnd).p->field;                         \
    (void)(&(hnd).p->field == _s);                      \
    raw_copy_to_guest(_d, _s, sizeof(*_s));             \
})

/* Copy sub-field of a structure from guest context via a guest handle. */
#define copy_field_from_guest(ptr, hnd, field) ({       \
    const typeof(&(ptr)->field) _s = &(hnd).p->field;   \
    typeof(&(ptr)->field) _d = &(ptr)->field;           \
    copy_from_guest(_d, _s, sizeof(*_d));               \
})

#define copy_to_guest(hnd, ptr, nr)                     \
    copy_to_guest_offset(hnd, 0, ptr, nr)

#define copy_from_guest(ptr, hnd, nr)                   \
    copy_from_guest_offset(ptr, hnd, 0, nr)

#define clear_guest(hnd, nr)                            \
    clear_guest_offset(hnd, 0, nr)
```

**底层实现**: `raw_copy_from_guest` 和 `raw_copy_to_guest` 函数负责将 GFN 转换为 MFN 并执行实际的复制操作。

## 二、Viridian Hypercall 使用 GPA

**Viridian Hypercall**（Hyper-V 兼容性）使用 **GPA (Guest Physical Address)** 来传递参数，这是一个更明确的例子。

**实现**:

```920:1000:xen/xen/arch/x86/hvm/viridian/viridian.c
int viridian_hypercall(struct cpu_user_regs *regs)
{
    struct vcpu *curr = current;
    struct domain *currd = curr->domain;
    struct viridian_domain *vd = currd->arch.hvm.viridian;
    int mode = hvm_guest_x86_mode(curr);
    unsigned long input_params_gpa, output_params_gpa;
    int rc = 0;
    union hypercall_input input;
    union hypercall_output output = {};

    ASSERT(is_viridian_domain(currd));

    switch ( mode )
    {
    case 8:
        input.raw = regs->rcx;
        input_params_gpa = regs->rdx;
        output_params_gpa = regs->r8;
        break;

    case 4:
        input.raw = (regs->rdx << 32) | regs->eax;
        input_params_gpa = (regs->rbx << 32) | regs->ecx;
        output_params_gpa = (regs->rdi << 32) | regs->esi;
        break;

    default:
        goto out;
    }

    switch ( input.call_code )
    {
    case HVCALL_NOTIFY_LONG_SPIN_WAIT:
        if ( !test_and_set_bit(_HCALL_spin_wait, vd->hypercall_flags) )
            printk(XENLOG_G_INFO "d%d: VIRIDIAN HVCALL_NOTIFY_LONG_SPIN_WAIT\n",
                   currd->domain_id);

        /*
         * See section 14.5.1 of the specification.
         */
        do_sched_op(SCHEDOP_yield, guest_handle_from_ptr(NULL, void));
        break;

    case HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE:
    case HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST:
        if ( !test_and_set_bit(_HCALL_flush, vd->hypercall_flags) )
            printk(XENLOG_G_INFO "%pd: VIRIDIAN HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE/LIST\n",
                   currd);

        rc = hvcall_flush(&input, &output, input_params_gpa,
                          output_params_gpa);
        break;

    case HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX:
    case HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX:
        if ( !test_and_set_bit(_HCALL_flush_ex, vd->hypercall_flags) )
            printk(XENLOG_G_INFO "%pd: VIRIDIAN HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE/LIST_EX\n",
                   currd);

        rc = hvcall_flush_ex(&input, &output, input_params_gpa,
                             output_params_gpa);
        break;

    case HVCALL_SEND_IPI:
        if ( !test_and_set_bit(_HCALL_ipi, vd->hypercall_flags) )
            printk(XENLOG_G_INFO "%pd: VIRIDIAN HVCALL_SEND_IPI\n",
                   currd);

        rc = hvcall_ipi(&input, &output, input_params_gpa,
                        output_params_gpa);
        break;

    case HVCALL_SEND_IPI_EX:
        if ( !test_and_set_bit(_HCALL_ipi_ex, vd->hypercall_flags) )
            printk(XENLOG_G_INFO "%pd: VIRIDIAN HVCALL_SEND_IPI_EX\n",
                   currd);

        rc = hvcall_ipi_ex(&input, &output, input_params_gpa,
                           output_params_gpa);
        break;
```

**特点**:
- **输入参数 GPA**: `input_params_gpa` - Guest 传递输入参数的物理地址
- **输出参数 GPA**: `output_params_gpa` - Guest 接收输出参数的物理地址
- Hypervisor 通过 GPA 访问 guest 内存中的参数结构

## 三、与 AMD SEV-ES GHCB 的相似性

### 3.1 GHCB 协议概述

**GHCB (Guest-Host Communication Block)** 是 AMD SEV-ES 中用于 Guest 与 Hypervisor 通信的协议。

**相似点**:

| 特性 | Xen Hypercall | AMD SEV-ES GHCB |
|------|---------------|-----------------|
| **地址空间** | GFN (Guest Frame Number) | GPA (Guest Physical Address) |
| **通信方式** | 物理页中存放请求/响应 | 物理页中存放请求/响应 |
| **地址转换** | P2M (GFN → MFN) | NPT (GPA → SPA) |
| **页大小** | 4KB 页 | 4KB 页 |
| **访问方式** | Hypervisor 映射后访问 | Hypervisor 映射后访问 |

### 3.2 设计模式对比

#### 3.2.1 Xen Hypercall 模式

```
Guest 侧:
  - 准备请求数据在 GFN 地址空间
  - 通过寄存器传递 GFN 指针
  - 调用 hypercall

Hypervisor 侧:
  - 接收 GFN 指针
  - 通过 P2M 转换为 MFN
  - 映射到 Hypervisor 地址空间
  - 访问请求数据
  - 将响应写回 GFN 地址空间
```

#### 3.2.2 AMD SEV-ES GHCB 模式

```
Guest 侧:
  - 准备请求数据在 GPA 地址空间（GHCB 页）
  - 通过 GHCB MSR 指定 GPA
  - 触发 #VC 异常

Hypervisor 侧:
  - 接收 GPA（通过 GHCB MSR）
  - 通过 NPT 转换为 SPA
  - 映射到 Hypervisor 地址空间
  - 访问 GHCB 页中的请求
  - 将响应写回 GHCB 页
```

### 3.3 共同的设计原则

**1. 地址空间隔离**:
- Guest 使用自己的地址空间（GFN/GPA）
- Hypervisor 负责地址转换
- 避免直接暴露机器地址

**2. 页级通信**:
- 使用完整的物理页作为通信缓冲区
- 简化地址管理和验证
- 提高安全性（页级保护）

**3. 异步通信**:
- Guest 写入请求，Hypervisor 读取
- Hypervisor 写入响应，Guest 读取
- 通过页内容而非寄存器传递复杂数据

**4. 地址转换抽象**:
- Guest 不需要知道机器地址
- Hypervisor 负责所有地址转换
- 支持内存迁移和重映射

## 四、Hypercall 参数传递机制

### 4.1 寄存器参数

**标准 hypercall** 使用寄存器传递参数（最多 5 个）：

**x86-64**:
- Hypercall 编号: `RAX`
- 参数 1-5: `RDI`, `RSI`, `RDX`, `R10`, `R8`
- 返回值: `RAX`

**x86-32**:
- Hypercall 编号: `EAX`
- 参数 1-5: `EBX`, `ECX`, `EDX`, `ESI`, `EDI`
- 返回值: `EAX`

### 4.2 指针参数

**当参数是指针时**，指针值本身是 guest 虚拟地址或物理地址，但 Hypervisor 需要：

1. **验证地址**: 确保地址在 guest 地址空间内
2. **地址转换**: 将 guest 地址转换为机器地址
3. **安全访问**: 使用 `copy_from_guest`/`copy_to_guest` 安全访问

**示例 - Memory Op Hypercall**:

```22:39:xen/xen/arch/x86/hvm/hypercall.c
long hvm_memory_op(unsigned long cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    long rc;

    switch ( cmd & MEMOP_CMD_MASK )
    {
    case XENMEM_machine_memory_map:
    case XENMEM_machphys_mapping:
        return -ENOSYS;
    }

    if ( !current->hcall_compat )
        rc = do_memory_op(cmd, arg);
    else
        rc = compat_memory_op(cmd, arg);

    return rc;
}
```

**`XEN_GUEST_HANDLE_PARAM(void) arg`** 是一个指向 guest 内存的指针，Hypervisor 通过 `copy_from_guest` 访问其内容。

## 五、P2M 映射的作用

### 5.1 GFN 到 MFN 转换

**P2M (Physical-to-Machine) 映射** 是 Xen 中实现 GFN 到 MFN 转换的核心机制。

**转换流程**:

```
Guest 物理地址 (GFN)
    ↓
P2M 查找
    ↓
机器物理地址 (MFN)
    ↓
映射到 Hypervisor 地址空间
    ↓
访问内存
```

**关键函数**: `get_page_from_gfn()`

### 5.2 地址空间隔离

**优势**:
- ✅ **安全性**: Guest 无法直接访问机器地址
- ✅ **灵活性**: 支持内存迁移、重映射
- ✅ **隔离性**: 不同 Guest 的 GFN 映射到不同的 MFN

## 六、设计优势

### 6.1 安全性

**地址空间隔离**:
- Guest 只能使用 GFN，无法直接访问 MFN
- Hypervisor 控制所有地址转换
- 防止 Guest 访问其他 Guest 或 Hypervisor 内存

### 6.2 灵活性

**内存管理**:
- 支持内存迁移（GFN 可以映射到不同的 MFN）
- 支持内存共享（多个 GFN 映射到同一 MFN）
- 支持内存热插拔

### 6.3 兼容性

**跨架构支持**:
- x86: GFN → MFN (通过 P2M)
- ARM: IPA → PA (通过 Stage 2 页表)
- 统一的接口抽象了架构差异

## 七、与 GHCB 的详细对比

### 7.1 地址表示

**Xen GFN**:
- 表示: Guest Frame Number（页号）
- 范围: Guest 物理地址空间
- 转换: P2M 表

**AMD SEV-ES GPA**:
- 表示: Guest Physical Address（完整地址）
- 范围: Guest 物理地址空间
- 转换: NPT (Nested Page Table)

### 7.2 通信协议

**Xen Hypercall**:
- 协议: Hypercall 编号 + 寄存器参数 + Guest Handle
- 触发: 直接 hypercall 指令（VMCALL/VMMCALL/SYSCALL）
- 同步: 同步执行

**AMD SEV-ES GHCB**:
- 协议: GHCB 页中的结构化数据
- 触发: #VC 异常（通过 GHCB MSR）
- 同步: 同步执行（通过异常处理）

### 7.3 使用场景

**Xen Hypercall**:
- 通用 hypercall 接口
- 所有 Guest 类型（PV, HVM, PVH）
- 所有架构（x86, ARM）

**AMD SEV-ES GHCB**:
- 特定于 SEV-ES 加密虚拟机
- 主要用于处理加密状态下的异常
- x86-64 架构

## 八、代码示例

### 8.1 Hypercall Page 创建示例

**Guest 侧**:
```c
// Guest 分配一个物理页
unsigned long gfn = allocate_guest_page();

// 通过 MSR 告诉 Hypervisor
wrmsr(MSR_XEN_HYPERCALL_PAGE, gfn << PAGE_SHIFT);

// 现在可以调用 hypercall
call hypercall_page + __HYPERVISOR_memory_op * 32;
```

**Hypervisor 侧**:
```c
// 在 guest_wrmsr_xen() 中
unsigned long gmfn = val >> PAGE_SHIFT;
struct page_info *page = get_page_from_gfn(d, gmfn, &t, P2M_ALLOC);
void *hypercall_page = __map_domain_page(page);
init_hypercall_page(d, hypercall_page);
```

### 8.2 Guest Handle 使用示例

**Hypercall 定义**:
```c
long do_memory_op(unsigned long cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    struct xen_memory_reservation reservation;

    // 从 guest 内存复制参数
    if ( copy_from_guest(&reservation, arg, 1) != 0 )
        return -EFAULT;

    // 处理请求...

    // 将结果写回 guest 内存
    if ( copy_to_guest(arg, &reservation, 1) != 0 )
        return -EFAULT;

    return 0;
}
```

## 九、总结

### 9.1 核心设计模式

1. **基于 GFN 的地址空间**: 所有 guest 内存访问都基于 GFN
2. **页级通信**: 使用物理页作为通信缓冲区
3. **地址转换抽象**: Hypervisor 负责所有地址转换
4. **安全访问接口**: 通过 `copy_from_guest`/`copy_to_guest` 安全访问

### 9.2 与 GHCB 的相似性

- ✅ 都使用 guest 物理地址空间
- ✅ 都在物理页中存放请求/响应
- ✅ 都通过地址转换访问内存
- ✅ 都提供页级安全隔离

### 9.3 设计优势

- **安全性**: 地址空间隔离
- **灵活性**: 支持内存迁移和重映射
- **兼容性**: 跨架构统一接口
- **性能**: 页级操作，减少转换开销

## 十、参考

- `xen/xen/arch/x86/traps.c` - Hypercall page 创建
- `xen/xen/include/xen/guest_access.h` - Guest 内存访问接口
- `xen/xen/arch/x86/hvm/viridian/viridian.c` - Viridian hypercall（GPA 示例）
- `xen/docs/guest-guide/x86/hypercall-abi.rst` - Hypercall ABI 文档
- [Xen Hypercall 笔记](./xen-hypercalls.md) - Hypercall 详细说明
- [AMD SEV-ES GHCB 规范](https://www.amd.com/system/files/TechDocs/56421-guest-hypervisor-communication-block-standardization.pdf) - GHCB 协议规范
