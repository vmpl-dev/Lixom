# FLASK 架构详解

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/xsm/flask/` - FLASK 实现
- `xen/xen/xsm/flask/ss/` - 安全服务器（Security Server）
- `xen/xen/xsm/flask/avc.c` - 访问向量缓存
- `xen/xen/xsm/flask/hooks.c` - Hook 实现

## 概述

**FLASK (FLux Advanced Security Kernel)** 是 Xen 中 XSM 框架的主要实现，提供强制访问控制（MAC - Mandatory Access Control）机制。FLASK 基于类型强制（Type Enforcement）模型，支持细粒度的安全策略定义和执行。

**核心观点**: FLASK 是一个策略驱动的安全系统，通过安全标识符（SID）、访问向量表（AVTAB）和访问向量缓存（AVC）来实现高效的访问控制决策。

## 一、FLASK 架构概览

### 1.1 整体架构

```
┌─────────────────────────────────────────┐
│      Xen Hypervisor 核心代码            │
│  (domain.c, memory.c, event.c...)       │
└──────────────┬──────────────────────────┘
               │ 调用 XSM Hook
               ▼
┌─────────────────────────────────────────┐
│         FLASK Hook 层                   │
│      (flask/hooks.c)                    │
│  - flask_domain_create()                │
│  - flask_evtchn_*()                     │
│  - flask_grant_*()                      │
└──────────────┬──────────────────────────┘
               │ 调用 AVC
               ▼
┌─────────────────────────────────────────┐
│    AVC (Access Vector Cache)            │
│      (flask/avc.c)                      │
│  - 缓存访问控制决策                      │
│  - 提高性能（避免重复策略查询）          │
└──────────────┬──────────────────────────┘
               │ 缓存未命中时查询
               ▼
┌─────────────────────────────────────────┐
│    安全服务器 (Security Server)         │
│      (flask/ss/services.c)             │
│  - security_compute_av()                 │
│  - security_transition_sid()             │
│  - security_load_policy()                │
└──────────────┬──────────────────────────┘
               │ 查询策略数据库
               ▼
┌─────────────────────────────────────────┐
│      策略数据库 (Policy Database)      │
│      (flask/ss/policydb.c)              │
│  - 访问向量表 (AVTAB)                   │
│  - SID 表 (SIDTAB)                      │
│  - 符号表 (SYMTAB)                      │
└─────────────────────────────────────────┘
```

### 1.2 核心组件

1. **Hook 层**: 实现 XSM Hook 接口
2. **AVC**: 访问向量缓存，提高性能
3. **安全服务器**: 计算访问控制决策
4. **策略数据库**: 存储和查询安全策略

## 二、FLASK 核心概念

### 2.1 安全上下文（Security Context）

**定义**: 安全上下文是描述主体（Subject）或客体（Object）安全属性的结构。

**结构**:

```29:34:xen/xen/xsm/flask/ss/context.h
struct context {
    u32 user;
    u32 role;
    u32 type;
    struct mls_range range;
};
```

**组成部分**:
- **User**: 用户标识符（如 `system_u`）
- **Role**: 角色标识符（如 `system_r`）
- **Type**: 类型标识符（如 `domU_t`）
- **MLS Range**: 多级安全范围（可选）

**示例**:
```
system_u:system_r:domU_t
  │        │        │
  │        │        └─ Type (类型)
  │        └────────── Role (角色)
  └─────────────────── User (用户)
```

### 2.2 安全标识符（SID - Security Identifier）

**定义**: SID 是安全上下文的唯一数字标识符。

**特点**:
- 32 位整数
- 内部使用，提高性能
- 通过 SID 表映射到安全上下文

**类型**:
- **Domain SID**: Domain 的当前 SID
- **Self SID**: Domain 访问自身时的 SID
- **Target SID**: Device Model 访问目标 Domain 时的 SID

**实现**:

```20:24:xen/xen/xsm/flask/include/objsec.h
struct domain_security_struct {
    u32 sid;               /* current SID */
    u32 self_sid;          /* SID for target when operating on DOMID_SELF */
    u32 target_sid;        /* SID for device model target domain */
};
```

### 2.3 安全类（Security Class）

**定义**: 安全类定义了不同类型的对象（Domain、事件通道、Grant 等）。

**Xen 中的安全类**:

```10:22:xen/xen/xsm/flask/policy/security_classes
class xen
class xen2
class domain
class domain2
class hvm
class mmu
class resource
class shadow
class event
class grant
class security
class version
class argo
```

**用途**: 每个安全类定义了不同的权限集合。

### 2.4 访问向量（Access Vector）

**定义**: 访问向量是权限的位图，表示主体对客体的访问权限。

**计算**: 通过 `security_compute_av()` 函数计算

```665:705:xen/xen/xsm/flask/ss/services.c
int security_compute_av(u32 ssid, u32 tsid, u16 tclass, u32 requested,
                        struct av_decision *avd)
{
    struct context *scontext = NULL, *tcontext = NULL;
    int rc = 0;

    if ( !ss_initialized )
    {
        avd->allowed = 0xffffffff;
        avd->auditallow = 0;
        avd->auditdeny = 0xffffffff;
        avd->seqno = latest_granting;
        return 0;
    }

    POLICY_RDLOCK;

    scontext = sidtab_search(&sidtab, ssid);
    if ( !scontext )
    {
        printk("security_compute_av:  unrecognized SID %d\n", ssid);
        rc = -EINVAL;
        goto out;
    }
    tcontext = sidtab_search(&sidtab, tsid);
    if ( !tcontext )
    {
        printk("security_compute_av:  unrecognized SID %d\n", tsid);
        rc = -EINVAL;
        goto out;
    }

    rc = context_struct_compute_av(scontext, tcontext, tclass, requested, avd);

    /* permissive domain? */
    if ( ebitmap_get_bit(&policydb.permissive_map, scontext->type) )
        avd->flags |= AVD_FLAGS_PERMISSIVE;
out:
    POLICY_RDUNLOCK;
    return rc;
}
```

## 三、FLASK 核心组件详解

### 3.1 访问向量缓存（AVC）

**功能**: 缓存访问控制决策，避免重复的策略查询。

**实现**: `xen/xen/xsm/flask/avc.c`

**缓存结构**:

```68:87:xen/xen/xsm/flask/avc.c
struct avc_entry {
    u32            ssid;
    u32            tsid;
    u16            tclass;
    struct av_decision    avd;
};

struct avc_node {
    struct avc_entry    ae;
    struct hlist_node   list; /* anchored in avc_cache->slots[i] */
    struct rcu_head     rhead;
};

struct avc_cache {
    struct hlist_head    slots[AVC_CACHE_SLOTS]; /* head for avc_node->list */
    spinlock_t        slots_lock[AVC_CACHE_SLOTS]; /* lock for writes */
    atomic_t        lru_hint;    /* LRU hint for reclaim scan */
    atomic_t        active_nodes;
    u32            latest_notif;    /* latest revocation notification */
};
```

**缓存查找流程**:

```722:768:xen/xen/xsm/flask/avc.c
int avc_has_perm_noaudit(u32 ssid, u32 tsid, u16 tclass, u32 requested,
                         struct av_decision *in_avd)
{
    struct avc_node *node;
    struct av_decision avd_entry, *avd;
    int rc = 0;
    u32 denied;

    BUG_ON(!requested);

    rcu_read_lock(&avc_rcu_lock);

    node = avc_lookup(ssid, tsid, tclass);
    if ( !node )
    {
        rcu_read_unlock(&avc_rcu_lock);

        if ( in_avd )
            avd = in_avd;
        else
            avd = &avd_entry;

        rc = security_compute_av(ssid,tsid,tclass,requested,avd);
        if ( rc )
            goto out;
        rcu_read_lock(&avc_rcu_lock);
        node = avc_insert(ssid,tsid,tclass,avd);
    } else {
        if ( in_avd )
            memcpy(in_avd, &node->ae.avd, sizeof(*in_avd));
        avd = &node->ae.avd;
    }

    denied = requested & ~(avd->allowed);

    if ( denied )
    {
        if ( !flask_enforcing || (avd->flags & AVD_FLAGS_PERMISSIVE) )
            avc_update_node(requested, ssid,tsid,tclass,avd->seqno);
        else
            rc = -EACCES;
    }

    rcu_read_unlock(&avc_rcu_lock);
 out:
    return rc;
}
```

**流程**:
1. 在 AVC 中查找（`avc_lookup`）
2. 如果命中，直接返回缓存的决策
3. 如果未命中，调用 `security_compute_av()` 计算
4. 将结果插入缓存（`avc_insert`）
5. 检查权限是否被拒绝

### 3.2 安全服务器（Security Server）

**功能**: 计算访问控制决策的核心组件。

**主要函数**:

#### 3.2.1 `security_compute_av()`

计算访问向量决策：

```68:69:xen/xen/xsm/flask/include/security.h
int security_compute_av(u32 ssid, u32 tsid, u16 tclass, u32 requested,
                                                    struct av_decision *avd);
```

**参数**:
- `ssid`: 源安全标识符
- `tsid`: 目标安全标识符
- `tclass`: 目标安全类
- `requested`: 请求的权限
- `avd`: 输出的访问决策

**返回的决策结构**:

```57:63:xen/xen/xsm/flask/include/security.h
struct av_decision {
    u32 allowed;
    u32 auditallow;
    u32 auditdeny;
    u32 seqno;
    u32 flags;
};
```

#### 3.2.2 `context_struct_compute_av()`

基于安全上下文计算访问向量：

```451:496:xen/xen/xsm/flask/ss/services.c
static int context_struct_compute_av(struct context *scontext,
				     struct context *tcontext,
				     u16 tclass,
				     u32 requested,
				     struct av_decision *avd)
{
    struct constraint_node *constraint;
    struct role_allow *ra;
    struct avtab_key avkey;
    struct avtab_node *node;
    struct class_datum *tclass_datum;
    struct ebitmap *sattr, *tattr;
    struct ebitmap_node *snode, *tnode;
    unsigned int i, j;

    /*
     * Initialize the access vectors to the default values.
     */
    avd->allowed = 0;
    avd->auditallow = 0;
    avd->auditdeny = 0xffffffff;
    avd->seqno = latest_granting;
    avd->flags = 0;

    /*
     * We do not presently support policydb.handle_unknown == allow in Xen.
     */
    if ( !tclass || tclass > policydb.p_classes.nprim )
        return -EINVAL;

    tclass_datum = policydb.class_val_to_struct[tclass - 1];

    /*
     * If a specific type enforcement rule was defined for
     * this permission check, then use it.
     */
    avkey.target_class = tclass;
    avkey.specified = AVTAB_AV;
    sattr = &policydb.type_attr_map[scontext->type - 1];
    tattr = &policydb.type_attr_map[tcontext->type - 1];
    ebitmap_for_each_positive_bit(sattr, snode, i)
    {
        ebitmap_for_each_positive_bit(tattr, tnode, j)
```

**计算步骤**:
1. 初始化访问向量为默认值（拒绝所有）
2. 验证安全类
3. 查找类型属性映射
4. 遍历源类型和目标类型的属性
5. 在 AVTAB 中查找匹配的规则
6. 应用约束条件
7. 返回最终决策

#### 3.2.3 `security_transition_sid()`

计算类型转换后的 SID：

```71:71:xen/xen/xsm/flask/include/security.h
int security_transition_sid(u32 ssid, u32 tsid, u16 tclass, u32 *out_sid);
```

**用途**: 计算 Domain 访问自身时的 SID（Self SID）

### 3.3 策略数据库（Policy Database）

**功能**: 存储和查询安全策略。

**主要组件**:

#### 3.3.1 访问向量表（AVTAB）

**定义**: 存储类型强制规则的哈希表。

**结构**:

```29:61:xen/xen/xsm/flask/ss/avtab.h
struct avtab_key {
    u16 source_type;    /* source type */
    u16 target_type;    /* target type */
    u16 target_class;    /* target object class */
#define AVTAB_ALLOWED     1
#define AVTAB_AUDITALLOW  2
#define AVTAB_AUDITDENY   4
#define AVTAB_AV         (AVTAB_ALLOWED | AVTAB_AUDITALLOW | AVTAB_AUDITDENY)
#define AVTAB_TRANSITION 16
#define AVTAB_MEMBER     32
#define AVTAB_CHANGE     64
#define AVTAB_TYPE       (AVTAB_TRANSITION | AVTAB_MEMBER | AVTAB_CHANGE)
#define AVTAB_ENABLED_OLD    0x80000000 /* reserved for used in cond_avtab */
#define AVTAB_ENABLED    0x8000 /* reserved for used in cond_avtab */
    u16 specified;    /* what field is specified */
};

struct avtab_datum {
    u32 data; /* access vector or type value */
};

struct avtab_node {
    struct avtab_key key;
    struct avtab_datum datum;
    struct avtab_node *next;
};

struct avtab {
    struct avtab_node **htable;
    u32 nel;    /* number of elements */
    u32 nslot;      /* number of hash slots */
    u16 mask;       /* mask to compute hash func */
};
```

**键（Key）组成**:
- `source_type`: 源类型
- `target_type`: 目标类型
- `target_class`: 目标安全类
- `specified`: 指定字段（ALLOWED、TRANSITION 等）

**数据（Datum）**: 访问向量（权限位图）或类型值

#### 3.3.2 SID 表（SIDTAB）

**定义**: 将 SID 映射到安全上下文的哈希表。

**结构**:

```16:34:xen/xen/xsm/flask/ss/sidtab.h
struct sidtab_node {
    u32 sid;        /* security identifier */
    struct context context;    /* security context structure */
    struct sidtab_node *next;
};

#define SIDTAB_HASH_BITS 7
#define SIDTAB_HASH_BUCKETS (1 << SIDTAB_HASH_BITS)
#define SIDTAB_HASH_MASK (SIDTAB_HASH_BUCKETS-1)

#define SIDTAB_SIZE SIDTAB_HASH_BUCKETS

struct sidtab {
    struct sidtab_node **htable;
    unsigned int nel;    /* number of elements */
    unsigned int next_sid;    /* next SID to allocate */
    unsigned char shutdown;
    spinlock_t lock;
};
```

**功能**:
- `sidtab_search()`: 根据 SID 查找安全上下文
- `sidtab_context_to_sid()`: 根据安全上下文查找或分配 SID
- `sidtab_insert()`: 插入新的 SID 映射

#### 3.3.3 符号表（SYMTAB）

**定义**: 存储策略中的符号（类型、角色、用户等）。

**类型**:
- 公共前缀（Common）
- 类（Class）
- 角色（Role）
- 类型（Type）
- 用户（User）
- 布尔值（Bool）
- 级别（Level）
- 类别（Category）

### 3.4 类型转换（Type Transition）

**功能**: 动态计算对象的安全上下文。

**类型转换场景**:

#### 3.4.1 Self 类型转换

**场景**: Domain 访问自身时

**示例**:
```
# 策略定义
type_transition domU_t domU_t : domain domU_t_self;

# 转换过程
源: domU_t (SID=10)
目标: domU_t (SID=10)
结果: domU_t_self (SID=11)
```

**实现**: `security_transition_sid()`

#### 3.4.2 Target 类型转换

**场景**: Device Model 访问目标 Domain 时

**示例**:
```
# 策略定义
type_transition domU_t dm_dom_t : domain domU_t_target;

# 转换过程
源: domU_t (SID=10)
目标: dm_dom_t (SID=20)
结果: domU_t_target (SID=21)
```

## 四、FLASK 访问控制流程

### 4.1 完整流程

```
1. Hook 调用
   ↓
2. 获取 SID
   - 源 SID: domain_sid(source_domain)
   - 目标 SID: domain_target_sid(source, target)
   ↓
3. AVC 查找
   - avc_has_perm(ssid, tsid, tclass, requested)
   ↓
4. 缓存命中？
   ├─ 是 → 返回缓存决策
   └─ 否 → 继续
   ↓
5. 安全服务器计算
   - security_compute_av(ssid, tsid, tclass, requested, &avd)
   ↓
6. 策略数据库查询
   - 查找 AVTAB
   - 应用约束
   - 计算最终决策
   ↓
7. 更新 AVC
   - avc_insert(ssid, tsid, tclass, avd)
   ↓
8. 返回决策
   - 允许或拒绝
```

### 4.2 示例：事件通道创建

```c
// 在 event_channel.c 中
int evtchn_alloc_unbound(domid_t dom, ...)
{
    // 1. 获取 Domain
    d = rcu_lock_domain_by_id(dom);

    // 2. XSM Hook 检查
    rc = xsm_evtchn_unbound(XSM_HOOK, current->domain, chn, id2);
    if ( rc )
        return rc;

    // ... 继续创建 ...
}
```

**FLASK Hook 实现**:

```c
// 在 flask/hooks.c 中
static int flask_evtchn_unbound(struct domain *d, struct evtchn *chn, domid_t id2)
{
    // 1. 获取 SID
    uint32_t ssid = domain_sid(current->domain);
    uint32_t tsid = domain_sid(d);

    // 2. AVC 检查
    return avc_has_perm(ssid, tsid, SECCLASS_EVENT,
                        EVENT__CREATE_UNBOUND, NULL);
}
```

## 五、FLASK 策略系统

### 5.1 策略文件结构

```
tools/flask/policy/
├── security_classes      # 安全类定义
├── access_vectors        # 访问向量定义
├── initial_sids          # 初始 SID 定义
├── modules/              # 策略模块
│   ├── xen.te           # Xen 类型定义
│   ├── dom0.te          # Domain 0 策略
│   ├── domU.te          # Domain U 策略
│   └── ...
└── policy.conf           # 编译后的策略
```

### 5.2 策略编译

**工具**: SELinux 的 `checkpolicy`

**过程**:
1. 解析策略源文件（`.te` 文件）
2. 生成访问向量表
3. 编译为二进制格式
4. 嵌入到 Hypervisor 或作为模块加载

### 5.3 策略加载

**方式**:
1. **编译时嵌入**: 通过 `CONFIG_XSM_FLASK_POLICY`
2. **启动时加载**: 通过 Multiboot 模块
3. **运行时加载**: 通过 `xl loadpolicy`

**实现**: `security_load_policy()`

## 六、FLASK 初始化

### 6.1 初始化流程

```1983:2023:xen/xen/xsm/flask/hooks.c
const struct xsm_ops *__init flask_init(
    const void *policy_buffer, size_t policy_size)
{
    int ret = -ENOENT;

    switch ( flask_bootparam )
    {
    case FLASK_BOOTPARAM_DISABLED:
        printk(XENLOG_INFO "Flask: Disabled at boot.\n");
        return NULL;

    case FLASK_BOOTPARAM_PERMISSIVE:
        flask_enforcing = 0;
        break;

    case FLASK_BOOTPARAM_ENFORCING:
    case FLASK_BOOTPARAM_LATELOAD:
        break;

    case FLASK_BOOTPARAM_INVALID:
    default:
        panic("Flask: Invalid value for flask= boot parameter.\n");
    }

    avc_init();

    if ( policy_size && flask_bootparam != FLASK_BOOTPARAM_LATELOAD )
        ret = security_load_policy(policy_buffer, policy_size);

    if ( ret && flask_bootparam == FLASK_BOOTPARAM_ENFORCING )
        panic("Unable to load FLASK policy\n");

    if ( ret )
        printk(XENLOG_INFO "Flask:  Access controls disabled until policy is loaded.\n");
    else if ( flask_enforcing )
        printk(XENLOG_INFO "Flask:  Starting in enforcing mode.\n");
    else
        printk(XENLOG_INFO "Flask:  Starting in permissive mode.\n");

    return &flask_ops;
}
```

**步骤**:
1. 解析启动参数（`flask=`）
2. 初始化 AVC
3. 加载策略
4. 设置强制模式
5. 注册 `flask_ops`

### 6.2 启动参数

**FLASK 模式**:

```44:50:xen/xen/xsm/flask/include/security.h
enum flask_bootparam_t {
    FLASK_BOOTPARAM_PERMISSIVE,
    FLASK_BOOTPARAM_ENFORCING,
    FLASK_BOOTPARAM_LATELOAD,
    FLASK_BOOTPARAM_DISABLED,
    FLASK_BOOTPARAM_INVALID,
};
```

**使用**:
```
flask=enforcing   # 强制模式（拒绝未授权访问）
flask=permissive  # 许可模式（记录但允许）
flask=disabled    # 禁用 FLASK
flask=late        # 延迟加载策略
```

## 七、FLASK 数据结构

### 7.1 策略数据库结构

```policydb
struct policydb {
    // 符号表
    struct symtab p_classes;      // 类
    struct symtab p_commons;      // 公共前缀
    struct symtab p_roles;        // 角色
    struct symtab p_types;        // 类型
    struct symtab p_users;        // 用户
    struct symtab p_bools;        // 布尔值

    // 访问向量表
    struct avtab te_avtab;        // 类型强制表

    // SID 表
    struct sidtab sidtab;         // SID 映射表

    // 其他
    struct ebitmap permissive_map; // 许可类型映射
    // ...
};
```

### 7.2 访问决策结构

```57:63:xen/xen/xsm/flask/include/security.h
struct av_decision {
    u32 allowed;
    u32 auditallow;
    u32 auditdeny;
    u32 seqno;
    u32 flags;
};
```

**字段说明**:
- `allowed`: 允许的权限位图
- `auditallow`: 需要审计的允许权限
- `auditdeny`: 需要审计的拒绝权限
- `seqno`: 序列号（用于缓存失效）
- `flags`: 标志（如 `AVD_FLAGS_PERMISSIVE`）

## 八、FLASK 性能优化

### 8.1 AVC 缓存

**目的**: 避免重复的策略查询

**效果**:
- 缓存命中率通常 > 95%
- 显著减少策略查询开销

**管理**:
- LRU 替换策略
- 缓存大小可配置
- 支持缓存统计

### 8.2 RCU 保护

**实现**: AVC 使用 RCU（Read-Copy-Update）保护

**优势**:
- 读操作无锁
- 写操作最小化锁定
- 提高并发性能

### 8.3 策略优化

**技术**:
- 类型属性（Type Attributes）
- 公共权限前缀（Common Permissions）
- 条件策略（Conditional Policy）

## 九、FLASK 与 SELinux 的关系

### 9.1 历史渊源

- **FLASK**: 最初由 NSA 开发
- **SELinux**: 基于 FLASK 架构
- **Xen FLASK**: 从 SELinux 移植到 Xen

### 9.2 兼容性

**策略格式**: 兼容 SELinux 策略格式

**工具**: 可以使用 SELinux 工具
- `checkpolicy` - 策略编译
- `audit2allow` - 从审计日志生成策略
- `sepolgen` - 策略生成工具

**概念**: 相同的安全模型
- 类型强制（Type Enforcement）
- 多级安全（MLS）
- 角色访问控制（RBAC）

### 9.3 差异

**环境差异**:
- SELinux: Linux 内核环境
- FLASK: Xen Hypervisor 环境

**对象差异**:
- SELinux: 文件、进程、网络等
- FLASK: Domain、事件通道、Grant 等

## 十、FLASK 策略示例

### 10.1 基本类型定义

```
# Domain 类型
type dom0_t, domain_type;
type domU_t, domain_type;
type isolated_domU_t, domain_type;
```

### 10.2 访问规则

```
# dom0 可以创建 domU
allow dom0_t domU_t : domain { create };

# domU 之间可以通信
allow domU_t domU_t : evtchn { create send };
allow domU_t domU_t : grant { map };

# isolated_domU 只能与 dom0 通信
allow isolated_domU_t dom0_t : evtchn { create send };
```

### 10.3 类型转换

```
# Self 类型转换
type_transition domU_t domU_t : domain domU_t_self;

# Target 类型转换
type_transition domU_t dm_dom_t : domain domU_t_target;
```

### 10.4 设备标签

```
# 定义设备类型
type nic_dev_t, resource_type;

# 允许 domU 使用
allow domU_t nic_dev_t : resource { use };
```

## 十一、FLASK 文件组织

### 11.1 核心文件

```
xen/xen/xsm/flask/
├── hooks.c              # Hook 实现
├── flask_op.c           # FLASK hypercall
├── avc.c                # 访问向量缓存
├── avc_ss.h             # AVC 安全服务器接口
├── include/
│   ├── security.h      # 安全服务器接口
│   ├── objsec.h        # 对象安全结构
│   ├── avc.h           # AVC 接口
│   └── ...
└── ss/                  # 安全服务器
    ├── services.c       # 安全服务实现
    ├── policydb.c       # 策略数据库
    ├── policydb.h       # 策略数据库定义
    ├── avtab.c          # 访问向量表
    ├── sidtab.c         # SID 表
    ├── symtab.c         # 符号表
    ├── context.h        # 安全上下文
    ├── mls.c            # 多级安全
    └── ...
```

### 11.2 策略文件

```
xen/tools/flask/policy/
├── security_classes     # 安全类定义
├── access_vectors        # 访问向量定义
├── initial_sids         # 初始 SID
├── modules/             # 策略模块
│   ├── xen.te          # Xen 类型
│   ├── dom0.te         # Domain 0
│   ├── domU.te         # Domain U
│   └── ...
└── policy.conf          # 编译后的策略
```

## 十二、FLASK 优势与特点

### 12.1 优势

1. **细粒度控制**: 可以控制 Domain 间的所有交互
2. **策略驱动**: 通过策略文件定义，无需修改代码
3. **高性能**: AVC 缓存显著提高性能
4. **可扩展**: 支持自定义策略模块
5. **审计能力**: 记录所有访问控制决策
6. **成熟稳定**: 基于 SELinux，经过长期验证

### 12.2 特点

1. **强制访问控制**: 策略强制执行，无法绕过
2. **类型强制**: 基于类型的安全模型
3. **默认拒绝**: 未明确允许的操作被拒绝
4. **策略分离**: 策略与代码分离

### 12.3 适用场景

**适合**:
- 多租户环境
- 需要强隔离的场景
- 合规性要求
- Domain 0 分离

**不适合**:
- 单租户环境
- 性能极度敏感的场景
- 简单部署

## 十三、总结

### 13.1 核心要点

1. **FLASK 是强制访问控制系统**: 基于类型强制模型
2. **策略驱动**: 通过策略文件定义安全规则
3. **高性能**: AVC 缓存提高性能
4. **SELinux 兼容**: 使用相同的策略格式和工具

### 13.2 架构特点

- **分层设计**: Hook → AVC → 安全服务器 → 策略数据库
- **缓存优化**: AVC 显著减少策略查询
- **类型转换**: 支持动态类型转换
- **可扩展性**: 支持自定义策略模块

### 13.3 关键组件

1. **AVC**: 访问向量缓存（性能关键）
2. **安全服务器**: 计算访问决策
3. **策略数据库**: 存储安全策略
4. **Hook 层**: 实现 XSM 接口

## 十四、参考

- `xen/xen/xsm/flask/` - FLASK 实现
- `xen/xen/xsm/flask/ss/` - 安全服务器
- `xen/xen/xsm/flask/avc.c` - AVC 实现
- `xen/docs/misc/xsm-flask.txt` - FLASK 配置文档
- [XSM 架构文档](./xsm-architecture.md) - XSM 框架说明
- [SELinux Project](http://selinuxproject.org) - SELinux 文档
