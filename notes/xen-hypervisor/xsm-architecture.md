# Xen 安全模块（XSM）架构

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/include/xsm/xsm.h` - XSM 框架定义
- `xen/xen/xsm/xsm_core.c` - XSM 核心实现
- `xen/xen/xsm/flask/` - FLASK 实现
- `xen/xen/xsm/silo.c` - SILO 实现
- `xen/xen/xsm/dummy.c` - Dummy 实现
- `xen/docs/misc/xsm-flask.txt` - FLASK 配置文档

## 概述

**XSM (Xen Security Module)** 是 Xen 的安全框架，类似于 Linux 的 LSM (Linux Security Module)。XSM 提供了细粒度的访问控制机制，允许管理员控制 Domain 之间的交互、设备访问和 Hypervisor 操作。

**核心观点**: XSM 是一个可插拔的安全框架，支持多种安全模型实现，其中 FLASK 是最主要的实现。

## 一、XSM 架构设计

### 1.1 框架层次

```
┌─────────────────────────────────────┐
│      Xen Hypervisor 核心代码        │
│  (domain.c, memory.c, event.c...)   │
└──────────────┬──────────────────────┘
               │ 调用 XSM Hook
               ▼
┌─────────────────────────────────────┐
│         XSM 框架层                  │
│    (xsm_core.c, xsm.h)              │
│    - xsm_ops 函数指针表             │
│    - Hook 包装函数                  │
└──────────────┬──────────────────────┘
               │ 路由到具体实现
               ▼
┌─────────────────────────────────────┐
│      XSM 实现模块                   │
│  ┌──────────┐  ┌──────────┐        │
│  │  FLASK   │  │   SILO   │        │
│  │ (默认)   │  │ (可选)   │        │
│  └──────────┘  └──────────┘        │
└─────────────────────────────────────┘
```

### 1.2 设计模式

XSM 采用**策略模式**（Strategy Pattern）：
- **框架层**: 定义统一的接口（`xsm_ops` 结构）
- **实现层**: 提供不同的安全策略实现
- **运行时选择**: 通过启动参数选择实现

### 1.3 与 Linux LSM 的关系

XSM 的设计基于 Linux 2.6.13.4 的 LSM 实现：

```1:4:xen/xen/include/xsm/xsm.h
/*
 *  This file contains the XSM hook definitions for Xen.
 *
 *  This work is based on the LSM implementation in Linux 2.6.13.4.
```

**相似点**:
- Hook 机制
- 函数指针表
- 可插拔架构

**不同点**:
- XSM 针对虚拟化环境
- 关注 Domain 间交互
- 支持设备直通控制

## 二、XSM 核心组件

### 2.1 XSM 操作结构（xsm_ops）

`xsm_ops` 是 XSM 的核心，定义了所有安全检查的接口：

```54:204:xen/xen/include/xsm/xsm.h
struct xsm_ops {
    int (*set_system_active)(void);
    void (*security_domaininfo)(struct domain *d,
                                struct xen_domctl_getdomaininfo *info);
    int (*domain_create)(struct domain *d, uint32_t ssidref);
    int (*getdomaininfo)(struct domain *d);
    int (*domctl_scheduler_op)(struct domain *d, int op);
    int (*sysctl_scheduler_op)(int op);
    int (*set_target)(struct domain *d, struct domain *e);
    int (*domctl)(struct domain *d, int cmd);
    int (*sysctl)(int cmd);
    int (*readconsole)(uint32_t clear);

    int (*evtchn_unbound)(struct domain *d, struct evtchn *chn, domid_t id2);
    int (*evtchn_interdomain)(struct domain *d1, struct evtchn *chn1,
                              struct domain *d2, struct evtchn *chn2);
    void (*evtchn_close_post)(struct evtchn *chn);
    int (*evtchn_send)(struct domain *d, struct evtchn *chn);
    int (*evtchn_status)(struct domain *d, struct evtchn *chn);
    int (*evtchn_reset)(struct domain *d1, struct domain *d2);

    int (*grant_mapref)(struct domain *d1, struct domain *d2, uint32_t flags);
    int (*grant_unmapref)(struct domain *d1, struct domain *d2);
    int (*grant_setup)(struct domain *d1, struct domain *d2);
    int (*grant_transfer)(struct domain *d1, struct domain *d2);
    int (*grant_copy)(struct domain *d1, struct domain *d2);
    int (*grant_query_size)(struct domain *d1, struct domain *d2);
    // ... 更多操作
};
```

**Hook 分类**:
- **Domain 管理**: `domain_create`, `domctl`, `sysctl`
- **事件通道**: `evtchn_*`
- **Grant 表**: `grant_*`
- **内存操作**: `memory_*`, `mmu_*`
- **设备操作**: `assign_device`, `map_domain_irq`
- **其他**: `console_io`, `kexec`, `profile`

### 2.2 Hook 调用机制

XSM 使用 `alternative_call` 机制进行 Hook 调用：

```212:215:xen/xen/include/xsm/xsm.h
static inline int xsm_set_system_active(void)
{
    return alternative_call(xsm_ops.set_system_active);
}
```

**优势**:
- 零开销（当 XSM 禁用时）
- 编译时优化
- 运行时选择实现

### 2.3 默认行为注解

XSM 定义了默认行为类型，用于文档化和编译时检查：

```35:42:xen/xen/include/xsm/xsm.h
enum xsm_default {
    XSM_HOOK,     /* Guests can normally access the hypercall */
    XSM_DM_PRIV,  /* Device model can perform on its target domain */
    XSM_TARGET,   /* Can perform on self or your target domain */
    XSM_PRIV,     /* Privileged - normally restricted to dom0 */
    XSM_XS_PRIV,  /* Xenstore domain - can do some privileged operations */
    XSM_OTHER     /* Something more complex */
};
```

## 三、XSM 实现模块

### 3.1 Dummy 实现

**位置**: `xen/xen/xsm/dummy.c`

**功能**:
- 默认实现（XSM 禁用时）
- 允许所有操作（无安全检查）
- 提供向后兼容性

**使用场景**:
- XSM 未编译
- 启动参数 `xsm=dummy`

### 3.2 FLASK 实现

**位置**: `xen/xen/xsm/flask/`

**全称**: FLux Advanced Security Kernel

**功能**:
- 强制访问控制（MAC - Mandatory Access Control）
- 基于类型的安全策略
- 支持 SELinux 兼容的策略格式

**特点**:
- ✅ **细粒度控制**: 可以控制 Domain 间的所有交互
- ✅ **策略驱动**: 通过策略文件定义安全规则
- ✅ **类型转换**: 支持动态类型转换
- ✅ **审计**: 记录所有访问控制决策

**配置**:
```bash
# 编译时启用
CONFIG_XSM=y
CONFIG_XSM_FLASK=y

# 启动参数
xsm=flask
flask=enforcing  # 或 permissive
```

**详细架构**: 参见 [FLASK 架构详解](./flask-architecture.md)

### 3.3 SILO 实现

**位置**: `xen/xen/xsm/silo.c`

**功能**:
- 简化的隔离模型
- 阻止非特权 Domain 之间的通信
- 轻量级实现

**特点**:
- ✅ **简单**: 实现简单，开销小
- ✅ **隔离**: 自动隔离非特权 Domain
- ✅ **ARM 默认**: ARM 架构默认使用 SILO

**使用场景**:
- 需要简单隔离的场景
- ARM 架构（默认）
- 启动参数 `xsm=silo`

## 四、FLASK 详细架构

### 4.1 FLASK 组件

```
┌─────────────────────────────────────┐
│         FLASK Hook 实现            │
│      (flask/hooks.c)               │
│  - flask_domain_create()           │
│  - flask_evtchn_*()                │
│  - flask_grant_*()                 │
└──────────────┬──────────────────────┘
               │ 调用
               ▼
┌─────────────────────────────────────┐
│      AVC (Access Vector Cache)     │
│      (flask/avc.c)                 │
│  - 缓存访问控制决策                 │
│  - 提高性能                         │
└──────────────┬──────────────────────┘
               │ 查询
               ▼
┌─────────────────────────────────────┐
│      策略数据库                     │
│      (flask/ss/policydb.c)          │
│  - 加载和解析策略                   │
│  - 提供策略查询接口                 │
└──────────────┬──────────────────────┘
               │ 基于
               ▼
┌─────────────────────────────────────┐
│      安全策略文件                   │
│  (tools/flask/policy/)              │
│  - 策略定义文件                     │
│  - 编译后的二进制策略               │
└─────────────────────────────────────┘
```

### 4.2 FLASK 策略系统

**策略定义位置**: `xen/tools/flask/policy/`

**主要文件**:
- `security_classes` - 安全类定义
- `access_vectors` - 访问向量定义
- `initial_sids` - 初始安全标识符
- `modules/` - 策略模块

**策略编译**:
```bash
# 使用 SELinux 的 checkpolicy 工具
checkpolicy -M -c $(POLICY_VERSION) -o policy.bin policy.conf
```

### 4.3 AVC (Access Vector Cache)

**功能**:
- 缓存访问控制决策
- 提高性能（避免重复策略查询）
- 统计信息收集

**实现**: `xen/xen/xsm/flask/avc.c`

**缓存结构**:
- 源安全标识符（SID）
- 目标安全标识符（SID）
- 安全类
- 权限位

### 4.4 安全标识符（SID）

**定义**: 每个 Domain 和资源都有唯一的安全标识符

**类型**:
- **Domain SID**: Domain 的安全标识符
- **Self SID**: Domain 访问自身时的 SID
- **Target SID**: Device Model 访问目标 Domain 时的 SID
- **Resource SID**: 设备、IRQ 等资源的 SID

**实现**: `xen/xen/xsm/flask/include/objsec.h`

## 五、XSM 初始化流程

### 5.1 启动参数解析

```58:77:xen/xen/xsm/xsm_core.c
static int __init cf_check parse_xsm_param(const char *s)
{
    int rc = 0;

    if ( !strcmp(s, "dummy") )
        xsm_bootparam = XSM_BOOTPARAM_DUMMY;
#ifdef CONFIG_XSM_FLASK
    else if ( !strcmp(s, "flask") )
        xsm_bootparam = XSM_BOOTPARAM_FLASK;
#endif
#ifdef CONFIG_XSM_SILO
    else if ( !strcmp(s, "silo") )
        xsm_bootparam = XSM_BOOTPARAM_SILO;
#endif
    else
        rc = -EINVAL;

    return rc;
}
custom_param("xsm", parse_xsm_param);
```

### 5.2 初始化流程

```79:139:xen/xen/xsm/xsm_core.c
static int __init xsm_core_init(const void *policy_buffer, size_t policy_size)
{
    const struct xsm_ops *ops = NULL;

#ifdef CONFIG_XSM_FLASK_POLICY
    if ( policy_size == 0 )
    {
        policy_buffer = xsm_flask_init_policy;
        policy_size = xsm_flask_init_policy_size;
    }
#endif

    if ( xsm_ops_registered != XSM_OPS_UNREGISTERED )
    {
        printk(XENLOG_ERR
               "Could not init XSM, xsm_ops register already attempted\n");
        return -EIO;
    }

    switch ( xsm_bootparam )
    {
    case XSM_BOOTPARAM_DUMMY:
        xsm_ops_registered = XSM_OPS_REGISTERED;
        break;

    case XSM_BOOTPARAM_FLASK:
        ops = flask_init(policy_buffer, policy_size);
        break;

    case XSM_BOOTPARAM_SILO:
        ops = silo_init();
        break;

    default:
        ASSERT_UNREACHABLE();
        break;
    }

    if ( ops )
    {
        xsm_ops_registered = XSM_OPS_REGISTERED;
        xsm_ops = *ops;
    }
    /*
     * This handles three cases,
     *   - dummy policy module was selected
     *   - a policy module does not provide all handlers
     *   - a policy module failed to init
     */
    xsm_fixup_ops(&xsm_ops);

    if ( xsm_ops_registered != XSM_OPS_REGISTERED )
    {
        xsm_ops_registered = XSM_OPS_REG_FAILED;
        printk(XENLOG_ERR
               "Could not init XSM, xsm_ops register failed\n");
        return -EFAULT;
    }

    return 0;
}
```

**步骤**:
1. 解析启动参数
2. 加载策略（FLASK）
3. 初始化实现模块
4. 注册 `xsm_ops`
5. 修复缺失的 Hook（`xsm_fixup_ops`）

## 六、XSM Hook 示例

### 6.1 Domain 创建 Hook

```c
// 在 domain.c 中
int domain_create(domid_t domid, ...)
{
    // ... 创建 Domain ...

    // XSM 检查
    rc = xsm_domain_create(XSM_HOOK, d, ssidref);
    if ( rc )
        goto fail;

    // ... 继续创建 ...
}
```

### 6.2 事件通道 Hook

```c
// 在 event_channel.c 中
int evtchn_alloc_unbound(domid_t dom, ...)
{
    // ... 分配事件通道 ...

    // XSM 检查
    rc = xsm_evtchn_unbound(XSM_HOOK, d, chn, id2);
    if ( rc )
        return rc;

    // ... 继续处理 ...
}
```

### 6.3 Grant 表 Hook

```c
// 在 grant_table.c 中
int gnttab_map_grant_ref(...)
{
    // ... 映射 Grant ...

    // XSM 检查
    rc = xsm_grant_mapref(XSM_HOOK, d1, d2, flags);
    if ( rc )
        return rc;

    // ... 继续映射 ...
}
```

## 七、FLASK 策略示例

### 7.1 基本策略结构

**Domain 类型定义**:
```
type dom0_t, domain_type;
type domU_t, domain_type;
type isolated_domU_t, domain_type;
```

**访问规则**:
```
# dom0 可以创建 domU
allow dom0_t domU_t : domain { create };

# domU 之间可以通信
allow domU_t domU_t : evtchn { create send };

# isolated_domU 只能与 dom0 通信
allow isolated_domU_t dom0_t : evtchn { create send };
```

### 7.2 类型转换

**Self 类型转换**:
```
# Domain 访问自身时使用 _self 类型
type_transition domU_t domU_t : domain domU_t_self;
```

**Target 类型转换**:
```
# Device Model 访问目标 Domain 时使用 _target 类型
type_transition domU_t dm_dom_t : domain domU_t_target;
```

### 7.3 设备标签

**PCI 设备标签**:
```
# 标签 PCI 设备
pcidevicecon 0xc800 system_u:object_r:nic_dev_t

# 允许 domU 使用该设备
allow domU_t nic_dev_t : resource { use };
```

## 八、XSM 配置选项

### 8.1 编译时配置

```289:366:xen/xen/common/Kconfig
config XSM
	bool "Xen Security Modules support"
	default ARM
	---help---
	  Enables the security framework known as Xen Security Modules which
	  allows administrators fine-grained control over a Xen domain and
	  its capabilities by defining permissible interactions between domains,
	  the hypervisor itself, and related resources such as memory and
	  devices.

	  If unsure, say N.

config XSM_FLASK
	def_bool y
	prompt "FLux Advanced Security Kernel support"
	depends on XSM
	---help---
	  Enables FLASK (FLux Advanced Security Kernel) as the access control
	  mechanism used by the XSM framework.  This provides a mandatory access
	  control framework by which security enforcement, isolation, and
	  auditing can be achieved with fine granular control via a security
	  policy.

	  If unsure, say Y.

config XSM_FLASK_AVC_STATS
	def_bool y
	prompt "Maintain statistics on the FLASK access vector cache" if EXPERT
	depends on XSM_FLASK
	---help---
	  Maintain counters on the access vector cache that can be viewed using
	  the FLASK_AVC_CACHESTATS sub-op of the xsm_op hypercall.  Disabling
	  this will save a tiny amount of memory and time to update the stats.

	  If unsure, say Y.

config XSM_FLASK_POLICY
	default y if "$(XEN_HAS_CHECKPOLICY)" = "y"
	depends on XSM_FLASK
	---help---
	  This includes a default XSM policy in the hypervisor so that the
	  bootloader does not need to load a policy to get sane behavior from an
	  XSM-enabled hypervisor.  If this is disabled, a policy must be
	  provided by the bootloader or by Domain 0.  Even if this is enabled, a
	  policy provided by the bootloader will override it.

	  This requires that the SELinux policy compiler (checkpolicy) be
	  available when compiling the hypervisor.

	  If unsure, say Y.

config XSM_SILO
	def_bool y
	prompt "SILO support"
	depends on XSM
	---help---
	  Enables SILO as the access control mechanism used by the XSM framework.
	  This is not the default module, add boot parameter xsm=silo to choose
	  it. This will deny any unmediated communication channels (grant tables
	  and event channels) between unprivileged VMs.

	  If unsure, say Y.

choice
	prompt "Default XSM implementation"
	depends on XSM
	default XSM_SILO_DEFAULT if XSM_SILO && ARM
	default XSM_FLASK_DEFAULT if XSM_FLASK
	default XSM_SILO_DEFAULT if XSM_SILO
	default XSM_DUMMY_DEFAULT
	config XSM_DUMMY_DEFAULT
		bool "Match non-XSM behavior"
	config XSM_FLASK_DEFAULT
		bool "FLux Advanced Security Kernel" if XSM_FLASK
	config XSM_SILO_DEFAULT
		bool "SILO" if XSM_SILO
endchoice
```

### 8.2 启动参数

**XSM 选择**:
```
xsm=dummy    # 使用 Dummy 实现（无安全检查）
xsm=flask    # 使用 FLASK 实现
xsm=silo     # 使用 SILO 实现
```

**FLASK 模式**:
```
flask=enforcing   # 强制模式（拒绝未授权访问）
flask=permissive  # 许可模式（记录但允许）
flask=disabled    # 禁用 FLASK
flask=lateload    # 延迟加载策略
```

## 九、XSM 应用场景

### 9.1 Domain 隔离

**场景**: 防止两个 Domain 之间通信

**实现**:
```
# 策略中不定义允许规则
# 默认拒绝所有通信
```

### 9.2 设备直通控制

**场景**: 控制哪些 Domain 可以使用哪些设备

**实现**:
```
# 标签设备
flask-label-pci 0000:03:02.0 system_u:object_r:nic_dev_t

# 策略允许
allow domU_t nic_dev_t : resource { use };
```

### 9.3 Domain 0 分离（Disaggregation）

**场景**: 将 Domain 0 的功能分离到不同 Domain

**实现**:
```
# 定义不同的管理 Domain 类型
type dom0_t, domain_type;      # 控制域
type driver_dom_t, domain_type; # 驱动域
type xenstore_dom_t, domain_type; # Xenstore 域

# 定义各自的权限
allow driver_dom_t domU_t : domain { assign_device };
allow xenstore_dom_t domU_t : domain { xenstore };
```

### 9.4 审计和监控

**场景**: 记录所有安全相关的操作

**实现**:
- FLASK 自动记录所有 AVC 拒绝
- 可以通过 `xl dmesg` 查看
- 支持 SELinux 工具（如 `audit2allow`）

## 十、XSM Hypercall

### 10.1 XSM Hypercall

**编号**: `__HYPERVISOR_xsm_op` (27)

**功能**: 提供 XSM 相关的操作接口

**实现**: `xen/xen/xsm/xsm_core.c`

```222:225:xen/xen/xsm/xsm_core.c
long do_xsm_op(XEN_GUEST_HANDLE_PARAM(void) op)
{
    return xsm_do_xsm_op(op);
}
```

### 10.2 FLASK 特定操作

**FLASK 操作类型**:
- `FLASK_LOAD` - 加载策略
- `FLASK_GETBOOL` - 获取布尔值
- `FLASK_SETBOOL` - 设置布尔值
- `FLASK_AVC_STATS` - 获取 AVC 统计
- `FLASK_AVC_HASHSTATS` - 获取 AVC 哈希统计

**实现**: `xen/xen/xsm/flask/flask_op.c`

## 十一、XSM 文件结构

### 11.1 核心文件

```
xen/xen/include/xsm/
├── xsm.h              # XSM 框架定义
└── dummy.h            # Dummy 实现定义

xen/xen/xsm/
├── xsm_core.c         # XSM 核心实现
├── xsm_policy.c       # 策略加载
├── dummy.c            # Dummy 实现
├── silo.c             # SILO 实现
└── flask/             # FLASK 实现
    ├── hooks.c        # Hook 实现
    ├── flask_op.c     # FLASK hypercall
    ├── avc.c          # AVC 实现
    ├── ss/            # 策略服务
    │   ├── policydb.c # 策略数据库
    │   ├── avtab.c    # 访问向量表
    │   └── ...
    └── include/       # FLASK 头文件
```

### 11.2 策略文件

```
xen/tools/flask/policy/
├── security_classes   # 安全类定义
├── access_vectors     # 访问向量定义
├── initial_sids       # 初始 SID
├── modules/           # 策略模块
│   ├── xen.te        # Xen 类型定义
│   ├── dom0.te       # Domain 0 策略
│   ├── domU.te       # Domain U 策略
│   └── ...
└── policy.conf        # 编译后的策略
```

## 十二、XSM 优势与限制

### 12.1 优势

1. **细粒度控制**: 可以控制 Domain 间的所有交互
2. **策略驱动**: 通过策略文件定义安全规则，无需修改代码
3. **可扩展性**: 支持自定义安全模型
4. **审计能力**: 记录所有安全决策
5. **隔离增强**: 支持 Domain 0 分离

### 12.2 限制

1. **性能开销**: 每次安全检查都有开销（AVC 缓存缓解）
2. **策略复杂性**: 编写和维护策略需要专业知识
3. **调试困难**: 策略错误可能导致系统无法正常工作
4. **兼容性**: 某些功能需要策略支持才能使用

### 12.3 适用场景

**适合使用 XSM**:
- 多租户环境
- 需要强隔离的场景
- 合规性要求
- Domain 0 分离

**不适合使用 XSM**:
- 单租户环境
- 性能敏感场景
- 简单部署

## 十三、总结

### 13.1 核心要点

1. **XSM 是安全框架**: 提供可插拔的安全机制
2. **FLASK 是主要实现**: 提供强制访问控制
3. **策略驱动**: 通过策略文件定义安全规则
4. **Hook 机制**: 在关键操作点插入安全检查
5. **零开销设计**: 禁用时无性能影响

### 13.2 架构特点

- **模块化**: 框架与实现分离
- **可扩展**: 支持多种安全模型
- **高效**: AVC 缓存提高性能
- **灵活**: 支持运行时策略加载

### 13.3 与 Linux SELinux 的关系

- **相似**: 都基于 FLASK 架构
- **兼容**: 使用相同的策略格式
- **工具**: 可以使用 SELinux 工具
- **概念**: 类型、角色、用户等概念相同

## 十四、参考

- `xen/xen/include/xsm/xsm.h` - XSM 框架定义
- `xen/xen/xsm/xsm_core.c` - XSM 核心实现
- `xen/xen/xsm/flask/` - FLASK 实现
- `xen/docs/misc/xsm-flask.txt` - FLASK 配置文档
- [FLASK 架构详解](./flask-architecture.md) - FLASK 详细架构
- [Xen Project Wiki - XSM](https://wiki.xenproject.org/wiki/Xen_Security_Modules)
- [SELinux Project](http://selinuxproject.org) - SELinux 文档
