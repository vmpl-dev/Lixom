# Xen Hypervisor 架构无关的通用启动流程

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/common/` - 通用代码目录
- `xen/xen/common/kernel.c` - 内核通用功能
- `xen/xen/common/domain.c` - Domain 管理
- `xen/xen/common/sched/core.c` - 调度器核心

## 概述

本文档分析 Xen Hypervisor 中**架构无关**的通用启动流程。这些代码在所有架构（x86、ARM、PPC、RISC-V）中共享，提供了 Xen 的核心功能。

## 一、通用启动流程概览

无论底层架构如何，Xen 的通用启动流程都遵循相似的模式：

```
架构特定初始化
    |
    v
[通用内存管理初始化]
    |
    v
[虚拟化子系统初始化]
    |
    v
[调度器初始化]
    |
    v
[系统域设置]
    |
    v
[Domain 0 创建]
    |
    v
[系统激活]
```

## 二、通用内存管理

### 2.1 内存管理初始化

**位置**: 各架构的 `setup.c` 中调用 `setup_mm()`

虽然 `setup_mm()` 的实现是架构特定的，但内存管理的**接口和数据结构**是通用的：

- **页帧管理**: `xen/xen/common/page_alloc.c`
- **内存分配器**: `xen/xen/common/xmalloc_tlsf.c`
- **内存域管理**: `xen/xen/common/memory.c`

### 2.2 启动分配器结束

```809:809:xen/xen/arch/arm/setup.c
    end_boot_allocator();
```

**通用功能**: 结束启动阶段的临时内存分配器，切换到正常的内存分配器。

### 2.3 系统状态转换

```811:815:xen/xen/arch/arm/setup.c
    /*
     * The memory subsystem has been initialized, we can now switch from
     * early_boot -> boot.
     */
    system_state = SYS_STATE_boot;
```

**通用状态机**: Xen 使用系统状态机管理启动过程：
- `SYS_STATE_early_boot`: 早期启动
- `SYS_STATE_boot`: 启动阶段
- `SYS_STATE_active`: 系统激活

**定义位置**: `xen/xen/common/kernel.c:29`

```29:29:xen/xen/common/kernel.c
enum system_state system_state = SYS_STATE_early_boot;
```

## 三、虚拟化子系统初始化

### 3.1 虚拟化初始化

```817:817:xen/xen/arch/arm/setup.c
    vm_init();
```

**通用功能**: 初始化虚拟化子系统，这是所有架构共享的核心功能。

**相关代码**: `xen/xen/common/domain.c` 中的虚拟化相关函数。

## 四、调度器初始化

### 4.1 任务队列子系统

```861:861:xen/xen/arch/arm/setup.c
    tasklet_subsys_init();
```

**通用功能**: 初始化任务队列子系统，用于延迟执行任务。

**位置**: `xen/xen/common/tasklet.c`

### 4.2 空闲域初始化

```874:874:xen/xen/arch/arm/setup.c
    init_idle_domain();
```

**通用功能**: 初始化空闲域（idle domain），这是 Xen 的核心调度实体。

**相关代码**: `xen/xen/common/sched/core.c`

```86:91:xen/xen/arch/arm/setup.c
static void __init init_idle_domain(void)
{
    scheduler_init();
    set_current(idle_vcpu[0]);
    /* TODO: setup_idle_pagetable(); */
}
```

**关键操作**:
- `scheduler_init()`: 初始化调度器
- `set_current()`: 设置当前 VCPU

### 4.3 调度器初始化

**位置**: `xen/xen/common/sched/core.c`

调度器是 Xen 的核心组件，负责：
- CPU 时间片分配
- VCPU 调度
- 负载均衡

## 五、RCU 初始化

```876:876:xen/xen/arch/arm/setup.c
    rcu_init();
```

**通用功能**: 初始化 RCU (Read-Copy-Update) 机制，用于无锁读取。

**位置**: `xen/xen/common/rcupdate.c`

**用途**:
- 保护共享数据结构
- 提供无锁读取路径
- 延迟释放机制

## 六、系统域设置

```878:878:xen/xen/arch/arm/setup.c
    setup_system_domains();
```

**通用功能**: 设置系统域（如 DOMID_XEN、DOMID_IO 等）。

**系统域类型**:
- **DOMID_XEN**: Xen 自身
- **DOMID_IO**: I/O 域
- **DOMID_COW**: 写时复制域（如果启用）

**位置**: `xen/xen/common/domain.c`

## 七、中断和定时器初始化

### 7.1 维护中断

```869:869:xen/xen/arch/arm/setup.c
    init_maintenance_interrupt();
```

**通用功能**: 初始化维护中断，用于系统维护任务。

### 7.2 定时器中断

```870:870:xen/xen/arch/arm/setup.c
    init_timer_interrupt();
```

**通用功能**: 初始化定时器中断。

### 7.3 定时器初始化

```872:872:xen/xen/arch/arm/setup.c
    timer_init();
```

**通用功能**: 初始化定时器子系统。

**位置**: `xen/xen/common/timer.c`

## 八、多核启动

### 8.1 准备多核

```883:883:xen/xen/arch/arm/setup.c
    smp_prepare_cpus();
```

**通用功能**: 准备多核启动。

**位置**: `xen/xen/common/smp.c`

### 8.2 启动所有 CPU

```891:901:xen/xen/arch/arm/setup.c
    for_each_present_cpu ( i )
    {
        if ( (num_online_cpus() < nr_cpu_ids) && !cpu_online(i) )
        {
            int ret = cpu_up(i);
            if ( ret != 0 )
                printk("Failed to bring up CPU %u (error %d)\n", i, ret);
        }
    }

    printk("Brought up %ld CPUs\n", (long)num_online_cpus());
```

**通用功能**: 启动所有可用的 CPU 核心。

**关键函数**: `cpu_up()` - 通用的 CPU 启动函数

## 九、初始化调用机制

### 9.1 预 SMP 初始化调用

```889:889:xen/xen/arch/arm/setup.c
    do_presmp_initcalls();
```

**通用功能**: 执行在 SMP 启动之前的初始化调用。

**位置**: `xen/xen/common/kernel.c:413`

```413:418:xen/xen/common/kernel.c
void __init do_presmp_initcalls(void)
{
    const initcall_t *call;
    for ( call = __initcall_start; call < __presmp_initcall_end; call++ )
        (*call)();
}
```

### 9.2 常规初始化调用

```918:918:xen/xen/arch/arm/setup.c
    do_initcalls();
```

**通用功能**: 执行常规初始化调用。

**位置**: `xen/xen/common/kernel.c:420`

```420:425:xen/xen/common/kernel.c
void __init do_initcalls(void)
{
    const initcall_t *call;
    for ( call = __presmp_initcall_end; call < __initcall_end; call++ )
        (*call)();
}
```

**初始化调用机制**: Xen 使用初始化调用机制，允许模块在特定阶段自动初始化。

## 十、Domain 创建（通用部分）

### 10.1 Domain 创建函数

**位置**: `xen/xen/common/domain.c:583`

```583:585:xen/xen/common/domain.c
struct domain *domain_create(domid_t domid,
                             struct xen_domctl_createdomain *config,
                             unsigned int flags)
```

**通用功能**: 创建 Domain 的通用函数，所有架构共享。

**重要说明**: Domain 0 和普通 Domain 使用**相同的创建函数**，区别仅在于 `flags` 参数。Domain 0 通过 `CDF_privileged` 标志获得特权。详见 [Domain 0 的本质](./domain0-essence.md)。

**关键步骤**:
1. 分配 Domain 结构体
2. 初始化 Domain 基本属性
3. 初始化事件通道
4. 初始化授权表
5. 初始化调度器
6. 添加到 Domain 列表

### 10.2 Domain 初始化流程

```708:769:xen/xen/common/domain.c
    init_status |= INIT_arch;

    if ( !is_idle_domain(d) )
    {
        watchdog_domain_init(d);
        init_status |= INIT_watchdog;

        err = -ENOMEM;
        d->iomem_caps = rangeset_new(d, "I/O Memory", RANGESETF_prettyprint_hex);
        d->irq_caps   = rangeset_new(d, "Interrupts", 0);
        if ( !d->iomem_caps || !d->irq_caps )
            goto fail;

        if ( (err = xsm_domain_create(XSM_HOOK, d, config->ssidref)) != 0 )
            goto fail;

        d->controller_pause_count = 1;
        atomic_inc(&d->pause_count);

        if ( (err = evtchn_init(d, config->max_evtchn_port)) != 0 )
            goto fail;
        init_status |= INIT_evtchn;

        if ( (err = grant_table_init(d, config->max_grant_frames,
                                     config->max_maptrack_frames,
                                     config->grant_opts)) != 0 )
            goto fail;
        init_status |= INIT_gnttab;

        if ( (err = argo_init(d)) != 0 )
            goto fail;

        err = -ENOMEM;

        d->pbuf = xzalloc_array(char, DOMAIN_PBUF_SIZE);
        if ( !d->pbuf )
            goto fail;

        if ( (err = sched_init_domain(d, config->cpupool_id)) != 0 )
            goto fail;

        if ( (err = late_hwdom_init(d)) != 0 )
            goto fail;

        /*
         * Must not fail beyond this point, as our caller doesn't know whether
         * the domain has been entered into domain_list or not.
         */

        spin_lock(&domlist_update_lock);
        pd = &domain_list; /* NB. domain_list maintained in order of domid. */
        for ( pd = &domain_list; *pd != NULL; pd = &(*pd)->next_in_list )
            if ( (*pd)->domain_id > d->domain_id )
                break;
        d->next_in_list = *pd;
        d->next_in_hashbucket = domain_hash[DOMAIN_HASH(domid)];
        rcu_assign_pointer(*pd, d);
        rcu_assign_pointer(domain_hash[DOMAIN_HASH(domid)], d);
        spin_unlock(&domlist_update_lock);

        memcpy(d->handle, config->handle, sizeof(d->handle));
    }

    return d;
```

**通用初始化步骤**:
1. **Watchdog 初始化**: `watchdog_domain_init()`
2. **I/O 内存和中断能力**: `rangeset_new()`
3. **XSM 初始化**: `xsm_domain_create()`
4. **事件通道初始化**: `evtchn_init()`
5. **授权表初始化**: `grant_table_init()`
6. **Argo 初始化**: `argo_init()` (如果启用)
7. **调度器初始化**: `sched_init_domain()`
8. **添加到 Domain 列表**: 维护全局 Domain 列表和哈希表

## 十一、系统激活

### 11.1 丢弃初始模块

```944:944:xen/xen/arch/arm/setup.c
    discard_initial_modules();
```

**通用功能**: 丢弃启动时使用的临时模块。

### 11.2 堆内存初始化

```946:946:xen/xen/arch/arm/setup.c
    heap_init_late();
```

**通用功能**: 初始化堆内存分配器。

### 11.3 跟踪缓冲区初始化

```948:948:xen/xen/arch/arm/setup.c
    init_trace_bufs();
```

**通用功能**: 初始化跟踪缓冲区（如果启用）。

**位置**: `xen/xen/common/trace.c`

### 11.4 构造函数初始化

```950:950:xen/xen/arch/arm/setup.c
    init_constructors();
```

**通用功能**: 调用 C++ 构造函数（如果使用 C++ 代码）。

### 11.5 XSM 系统激活

```957:958:xen/xen/arch/arm/setup.c
    if ( (rc = xsm_set_system_active()) != 0 )
        panic("xsm: unable to switch to SYSTEM_ACTIVE privilege: %d\n", rc);
```

**通用功能**: 将 XSM (Xen Security Module) 切换到系统激活状态。

### 11.6 系统状态激活

```960:960:xen/xen/arch/arm/setup.c
    system_state = SYS_STATE_active;
```

**通用功能**: 将系统状态设置为激活。

### 11.7 取消暂停所有域

```962:963:xen/xen/arch/arm/setup.c
    for_each_domain( d )
        domain_unpause_by_systemcontroller(d);
```

**通用功能**: 取消暂停所有域，允许它们运行。

## 十二、通用核心组件

### 12.1 事件通道 (Event Channel)

**位置**: `xen/xen/common/event_channel.c`

**功能**:
- 域间通信机制
- 虚拟中断
- 通知机制

### 12.2 授权表 (Grant Table)

**位置**: `xen/xen/common/grant_table.c`

**功能**:
- 域间内存共享
- 安全的页面共享机制

### 12.3 调度器

**位置**: `xen/xen/common/sched/`

**支持的调度算法**:
- Credit Scheduler
- Credit2 Scheduler
- RT Scheduler
- ARINC653 Scheduler
- Null Scheduler

### 12.4 内存管理

**位置**: `xen/xen/common/memory.c`

**功能**:
- 域内存管理
- 内存分配和回收
- 内存共享（如果启用）

### 12.5 软中断 (Softirq)

**位置**: `xen/xen/common/softirq.c`

**功能**:
- 延迟中断处理
- 定时器处理
- RCU 回调

## 十三、通用启动流程总结

### 13.1 启动阶段划分

1. **早期启动** (`SYS_STATE_early_boot`):
   - 架构特定初始化
   - 基本内存管理

2. **启动阶段** (`SYS_STATE_boot`):
   - 虚拟化子系统初始化
   - 调度器初始化
   - Domain 0 创建

3. **激活阶段** (`SYS_STATE_active`):
   - 系统完全激活
   - 所有域可以运行

### 13.2 通用设计原则

1. **分层设计**: 架构特定层 → 通用层
2. **模块化**: 使用初始化调用机制
3. **状态机**: 使用系统状态管理启动过程
4. **可扩展性**: 支持多种调度算法和功能

## 十四、参考

- `xen/xen/common/kernel.c` - 内核通用功能
- `xen/xen/common/domain.c` - Domain 管理
- `xen/xen/common/sched/core.c` - 调度器核心
- `xen/xen/common/memory.c` - 内存管理
- `xen/xen/common/event_channel.c` - 事件通道
