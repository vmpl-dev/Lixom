# vCPU 生命周期管理与 Domain 启动流程

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/common/domain.c` - Domain 和 vCPU 管理
- `xen/xen/common/sched/core.c` - 调度器核心
- `xen/xen/arch/x86/domain.c` - x86 vCPU 架构特定代码
- `xen/xen/arch/arm/domain.c` - ARM vCPU 架构特定代码
- `xen/xen/arch/x86/smpboot.c` - x86 SMP 启动
- `xen/xen/arch/arm/smpboot.c` - ARM SMP 启动
- `xen/xen/include/public/vcpu.h` - vCPU 公共接口

## 概述

本文档详细分析 Xen Hypervisor 中 vCPU 的生命周期管理、Domain 的启动流程以及 SMP（对称多处理）支持机制。

## 一、vCPU 生命周期

### 1.1 vCPU 状态

vCPU 有以下主要状态：

**运行状态 (Runstate)**:
- **`RUNSTATE_offline`**: vCPU 离线，未初始化
- **`RUNSTATE_running`**: vCPU 正在物理 CPU 上运行
- **`RUNSTATE_runnable`**: vCPU 可运行，但未在物理 CPU 上运行
- **`RUNSTATE_blocked`**: vCPU 被阻塞（等待事件）

**暂停标志 (Pause Flags)**:
- **`_VPF_down`**: vCPU 已下线（通过 `VCPUOP_down`）
- **`_VPF_blocked`**: vCPU 被阻塞
- **`_VPF_paused`**: vCPU 被暂停

**初始化状态**:
- **`is_initialised`**: vCPU 是否已初始化（通过 `VCPUOP_initialise`）
- **`is_running`**: vCPU 是否正在运行

### 1.2 vCPU 创建

**位置**: `xen/xen/common/domain.c:224`

```224:306:xen/xen/common/domain.c
struct vcpu *vcpu_create(struct domain *d, unsigned int vcpu_id)
{
    struct vcpu *v;

    /*
     * Sanity check some input expectations:
     * - vcpu_id should be bounded by d->max_vcpus, and not previously
     *   allocated.
     * - VCPUs should be tightly packed and allocated in ascending order,
     *   except for the idle domain which may vary based on PCPU numbering.
     */
    if ( vcpu_id >= d->max_vcpus || d->vcpu[vcpu_id] ||
         (!is_idle_domain(d) && vcpu_id && !d->vcpu[vcpu_id - 1]) )
    {
        ASSERT_UNREACHABLE();
        return NULL;
    }

    if ( (v = alloc_vcpu_struct(d)) == NULL )
        return NULL;

    v->domain = d;
    v->vcpu_id = vcpu_id;
    v->dirty_cpu = VCPU_CPU_CLEAN;

    rwlock_init(&v->virq_lock);

    tasklet_init(&v->continue_hypercall_tasklet, NULL, NULL);

    grant_table_init_vcpu(v);

    if ( is_idle_domain(d) )
    {
        v->runstate.state = RUNSTATE_running;
        v->new_state = RUNSTATE_running;
    }
    else
    {
        v->runstate.state = RUNSTATE_offline;
        v->runstate.state_entry_time = NOW();
        set_bit(_VPF_down, &v->pause_flags);
        vcpu_info_reset(v);
        init_waitqueue_vcpu(v);
    }

    if ( sched_init_vcpu(v) != 0 )
        goto fail_wq;

    if ( vmtrace_alloc_buffer(v) != 0 )
        goto fail_wq;

    if ( arch_vcpu_create(v) != 0 )
        goto fail_sched;

    d->vcpu[vcpu_id] = v;
    if ( vcpu_id != 0 )
    {
        int prev_id = v->vcpu_id - 1;
        while ( (prev_id >= 0) && (d->vcpu[prev_id] == NULL) )
            prev_id--;
        BUG_ON(prev_id < 0);
        v->next_in_list = d->vcpu[prev_id]->next_in_list;
        d->vcpu[prev_id]->next_in_list = v;
    }

    /* Must be called after making new vcpu visible to for_each_vcpu(). */
    vcpu_check_shutdown(v);

    return v;

 fail_sched:
    sched_destroy_vcpu(v);
 fail_wq:
    destroy_waitqueue_vcpu(v);

    /* Must not hit a continuation in this context. */
    if ( vcpu_teardown(v) )
        ASSERT_UNREACHABLE();

    vcpu_destroy(v);

    return NULL;
}
```

**关键步骤**:
1. **参数检查**: 验证 vcpu_id 的有效性和顺序
2. **分配结构**: 分配 vCPU 数据结构
3. **初始化基本字段**: 设置 domain、vcpu_id、dirty_cpu
4. **初始化锁和任务**: 初始化虚拟 IRQ 锁、任务等
5. **初始化授权表**: 初始化 vCPU 的授权表
6. **设置初始状态**:
   - **空闲域**: 设置为 `RUNSTATE_running`
   - **普通域**: 设置为 `RUNSTATE_offline`，设置 `_VPF_down` 标志
7. **调度器初始化**: 初始化调度器相关结构
8. **架构特定初始化**: 调用 `arch_vcpu_create()`
9. **添加到 Domain**: 将 vCPU 添加到 Domain 的 vCPU 列表

### 1.3 vCPU 初始化 (VCPUOP_initialise)

**位置**: `xen/xen/common/domain.c:1822`

```1822:1831:xen/xen/common/domain.c
    case VCPUOP_initialise:
        if ( v->vcpu_info_area.map == &dummy_vcpu_info )
            return -EINVAL;

        rc = arch_initialise_vcpu(v, arg);
        if ( rc == -ERESTART )
            rc = hypercall_create_continuation(__HYPERVISOR_vcpu_op, "iih",
                                               cmd, vcpuid, arg);

        break;
```

**功能**: 初始化 vCPU 的架构特定状态（寄存器、页表等）

**参数**:
- **PV/ARM**: `vcpu_guest_context` 结构
- **x86 HVM**: `vcpu_hvm_context` 结构

**关键点**:
- 每个 vCPU 只能初始化一次
- 初始化后 vCPU 不会自动运行，需要调用 `VCPUOP_up`

### 1.4 vCPU 上线 (VCPUOP_up)

**位置**: `xen/xen/common/domain.c:1833`

```1833:1852:xen/xen/common/domain.c
    case VCPUOP_up:
#ifdef CONFIG_X86
        if ( pv_shim )
            rc = continue_hypercall_on_cpu(0, pv_shim_cpu_up, v);
        else
#endif
        {
            bool wake = false;

            domain_lock(d);
            if ( !v->is_initialised )
                rc = -EINVAL;
            else
                wake = test_and_clear_bit(_VPF_down, &v->pause_flags);
            domain_unlock(d);
            if ( wake )
                vcpu_wake(v);
        }

        break;
```

**功能**: 使 vCPU 可运行

**关键步骤**:
1. **检查初始化状态**: 确保 vCPU 已初始化
2. **清除下线标志**: 清除 `_VPF_down` 标志
3. **唤醒 vCPU**: 调用 `vcpu_wake()` 使 vCPU 可调度

**唤醒机制** (`vcpu_wake()`):

```965:1002:xen/xen/common/sched/core.c
void vcpu_wake(struct vcpu *v)
{
    unsigned long flags;
    spinlock_t *lock;
    struct sched_unit *unit = v->sched_unit;

    TRACE_2D(TRC_SCHED_WAKE, v->domain->domain_id, v->vcpu_id);

    rcu_read_lock(&sched_res_rculock);

    lock = unit_schedule_lock_irqsave(unit, &flags);

    if ( likely(vcpu_runnable(v)) )
    {
        if ( v->runstate.state >= RUNSTATE_blocked )
            vcpu_runstate_change(v, RUNSTATE_runnable, NOW());
        /*
         * Call sched_wake() unconditionally, even if unit is running already.
         * We might have not been de-scheduled after vcpu_sleep_nosync_locked()
         * and are now to be woken up again.
         */
        sched_wake(unit_scheduler(unit), unit);
        if ( unit->is_running && !v->is_running && !v->force_context_switch )
        {
            v->force_context_switch = true;
            cpu_raise_softirq(v->processor, SCHED_SLAVE_SOFTIRQ);
        }
    }
    else if ( !(v->pause_flags & VPF_blocked) )
    {
        if ( v->runstate.state == RUNSTATE_blocked )
            vcpu_runstate_change(v, RUNSTATE_offline, NOW());
    }

    unit_schedule_unlock_irqrestore(lock, flags, unit);

    rcu_read_unlock(&sched_res_rculock);
}
```

**关键操作**:
- **状态转换**: 将状态从 `RUNSTATE_blocked` 或 `RUNSTATE_offline` 转换为 `RUNSTATE_runnable`
- **调度器唤醒**: 调用 `sched_wake()` 通知调度器
- **上下文切换**: 如果 vCPU 正在运行但需要切换，触发软中断

### 1.5 vCPU 下线 (VCPUOP_down)

**位置**: `xen/xen/common/domain.c:1854`

```1854:1876:xen/xen/common/domain.c
    case VCPUOP_down:
        for_each_vcpu ( d, v )
            if ( v->vcpu_id != vcpuid && !test_bit(_VPF_down, &v->pause_flags) )
            {
               rc = 1;
               break;
            }

        if ( !rc ) /* Last vcpu going down? */
        {
            domain_shutdown(d, SHUTDOWN_poweroff);
            break;
        }

        rc = 0;
        v = d->vcpu[vcpuid];

#ifdef CONFIG_X86
        if ( pv_shim )
            rc = continue_hypercall_on_cpu(0, pv_shim_cpu_down, v);
        else
#endif
            if ( !test_and_set_bit(_VPF_down, &v->pause_flags) )
```

**功能**: 使 vCPU 不可运行

**关键步骤**:
1. **检查其他 vCPU**: 如果这是最后一个运行的 vCPU，关闭 Domain
2. **设置下线标志**: 设置 `_VPF_down` 标志
3. **异步操作**: vCPU 可能不会立即停止运行

**注意事项**:
- 操作是异步的，vCPU 可能仍在运行
- 如果 vCPU 自己调用，操作是同步的
- 下线后 vCPU 仍持有内存引用（页表、GDT 等）

### 1.6 vCPU 销毁

**位置**: `xen/xen/common/domain.c:219`

```219:222:xen/xen/common/domain.c
static void vcpu_destroy(struct vcpu *v)
{
    free_vcpu_struct(v);
}
```

**销毁流程**:
1. **架构特定清理**: `arch_vcpu_destroy()`
2. **调度器清理**: `sched_destroy_vcpu()`
3. **释放结构**: `free_vcpu_struct()`

## 二、Domain 启动流程

### 2.1 Domain 暂停/恢复机制

Domain 使用引用计数机制管理暂停状态：

**暂停计数**: `d->pause_count` - Domain 暂停计数
**控制器暂停计数**: `d->controller_pause_count` - 工具栈暂停计数

**初始状态**: Domain 创建时，`controller_pause_count` 初始化为 1，Domain 处于暂停状态。

### 2.2 Domain 暂停

**位置**: `xen/xen/common/domain.c:1295`

```1295:1320:xen/xen/common/domain.c
static void _domain_pause(struct domain *d, bool sync)
{
    struct vcpu *v;

    atomic_inc(&d->pause_count);

    if ( sync )
        for_each_vcpu ( d, v )
            vcpu_sleep_sync(v);
    else
        for_each_vcpu ( d, v )
            vcpu_sleep_nosync(v);

    arch_domain_pause(d);
}

void domain_pause(struct domain *d)
{
    ASSERT(d != current->domain);
    _domain_pause(d, true /* sync */);
}

void domain_pause_nosync(struct domain *d)
{
    _domain_pause(d, false /* nosync */);
}
```

**关键步骤**:
1. **增加暂停计数**: 原子增加 `pause_count`
2. **暂停所有 vCPU**:
   - **同步**: `vcpu_sleep_sync()` - 等待 vCPU 进入睡眠状态
   - **异步**: `vcpu_sleep_nosync()` - 不等待，立即返回
3. **架构特定暂停**: 调用 `arch_domain_pause()`

### 2.3 Domain 恢复

**位置**: `xen/xen/common/domain.c:1322`

```1322:1331:xen/xen/common/domain.c
void domain_unpause(struct domain *d)
{
    struct vcpu *v;

    arch_domain_unpause(d);

    if ( atomic_dec_and_test(&d->pause_count) )
        for_each_vcpu( d, v )
            vcpu_wake(v);
}
```

**关键步骤**:
1. **架构特定恢复**: 调用 `arch_domain_unpause()`
2. **减少暂停计数**: 原子减少 `pause_count`
3. **唤醒 vCPU**: 如果暂停计数为 0，唤醒所有 vCPU

### 2.4 Domain 创建完成

**位置**: `xen/xen/common/domain.c:1367`

```1367:1402:xen/xen/common/domain.c
int domain_unpause_by_systemcontroller(struct domain *d)
{
    int old, new, prev = d->controller_pause_count;

    do
    {
        old = prev;
        new = old - 1;

        if ( new < 0 )
            return -EINVAL;

        prev = cmpxchg(&d->controller_pause_count, old, new);
    } while ( prev != old );

    /*
     * d->controller_pause_count is initialised to 1, and the toolstack is
     * responsible for making one unpause hypercall when it wishes the guest
     * to start running.
     *
     * All other toolstack operations should make a pair of pause/unpause
     * calls and rely on the reference counting here.
     *
     * Creation is considered finished when the controller reference count
     * first drops to 0.
     */
    if ( new == 0 && !d->creation_finished )
    {
        d->creation_finished = true;
        arch_domain_creation_finished(d);
    }

    domain_unpause(d);

    return 0;
}
```

**关键机制**:
- **初始状态**: Domain 创建时，`controller_pause_count = 1`
- **创建完成**: 当 `controller_pause_count` 首次降到 0 时，标记 Domain 创建完成
- **工具栈责任**: 工具栈负责调用一次 `unpause` 来启动 Domain

### 2.5 Domain 启动流程

**完整流程**:

```
1. Domain 创建
   ├─> domain_create()
   ├─> 创建 Domain 结构
   ├─> 创建 vCPU (vcpu_create())
   └─> controller_pause_count = 1 (Domain 暂停)

2. Domain 配置
   ├─> 设置内存
   ├─> 加载内核
   ├─> 配置设备
   └─> 初始化 vCPU (VCPUOP_initialise)

3. Domain 启动
   ├─> VCPUOP_up (使 vCPU 可运行)
   └─> domain_unpause_by_systemcontroller()
       ├─> controller_pause_count = 0
       ├─> creation_finished = true
       └─> domain_unpause()
           └─> vcpu_wake() (唤醒所有 vCPU)

4. Domain 运行
   └─> 调度器开始调度 vCPU
```

### 2.6 Domain 0 启动

Domain 0 的启动流程略有不同：

**启动时创建**: Domain 0 在 Xen 启动时创建，而不是通过工具栈

**关键代码** (`xen/xen/arch/arm/setup.c:960`):

```960:963:xen/xen/arch/arm/setup.c
    system_state = SYS_STATE_active;

    for_each_domain( d )
        domain_unpause_by_systemcontroller(d);
```

**启动时机**: 系统状态切换到 `SYS_STATE_active` 后，自动取消暂停所有 Domain

## 三、SMP 支持

### 3.1 CPU 状态

Xen 维护以下 CPU 状态：

**CPU 映射**:
- **`cpu_possible_map`**: 可能存在的 CPU（硬件支持）
- **`cpu_present_map`**: 存在的 CPU（已检测到）
- **`cpu_online_map`**: 在线的 CPU（已启动并可用）

### 3.2 x86 SMP 启动

**位置**: `xen/xen/arch/x86/setup.c:1954`

```1954:1985:xen/xen/arch/x86/setup.c
    if ( !pv_shim )
    {
        for_each_present_cpu ( i )
        {
            /* Set up cpu_to_node[]. */
            srat_detect_node(i);
            /* Set up node_to_cpumask based on cpu_to_node[]. */
            numa_add_cpu(i);

            if ( (park_offline_cpus || num_online_cpus() < max_cpus) &&
                 !cpu_online(i) )
            {
                ret = cpu_up(i);
                if ( ret != 0 )
                    printk("Failed to bring up CPU %u (error %d)\n", i, ret);
                else if ( num_online_cpus() > max_cpus ||
                          (!opt_smt &&
                           cpu_data[i].compute_unit_id == INVALID_CUID &&
                           cpumask_weight(per_cpu(cpu_sibling_mask, i)) > 1) )
                {
                    ret = cpu_down(i);
                    if ( !ret )
                        ++num_parked;
                    else
                        printk("Could not re-offline CPU%u (%d)\n", i, ret);
                }
            }
        }
    }

    printk("Brought up %ld CPUs\n", (long)num_online_cpus());
    if ( num_parked )
        printk(XENLOG_INFO "Parked %u CPUs\n", num_parked);
    smp_cpus_done();
```

**关键步骤**:
1. **遍历所有 CPU**: 遍历所有存在的 CPU
2. **NUMA 设置**: 设置 CPU 到 NUMA 节点的映射
3. **CPU 上线**: 调用 `cpu_up()` 启动 CPU
4. **CPU 下线**: 如果超过最大 CPU 数或禁用 SMT，下线 CPU

**CPU 上线流程** (`cpu_up()`):
1. **分配栈**: 为 CPU 分配栈
2. **发送 IPI**: 发送中断处理器间中断（IPI）唤醒 CPU
3. **等待上线**: 等待 CPU 进入 `CPU_STATE_ONLINE` 状态

**辅助 CPU 启动** (`xen/xen/arch/x86/smpboot.c:200`):

```200:222:xen/xen/arch/x86/smpboot.c
        cpu_error = rc;
        goto halt;
    }

    if ( (rc = hvm_cpu_up()) != 0 )
    {
        printk("CPU%d: Failed to initialise HVM. Not coming online.\n", cpu);
        cpu_error = rc;
    halt:
        clear_local_APIC();
        spin_debug_enable();
        play_dead();
    }

    /* Allow the master to continue. */
    set_cpu_state(CPU_STATE_CALLIN);

    synchronize_tsc_slave(cpu);

    /* And wait for our final Ack. */
    while ( cpu_state != CPU_STATE_ONLINE )
        cpu_relax();
```

**关键操作**:
1. **HVM 初始化**: 初始化硬件虚拟化支持
2. **状态设置**: 设置 CPU 状态为 `CPU_STATE_CALLIN`
3. **TSC 同步**: 同步时间戳计数器
4. **等待确认**: 等待主 CPU 确认上线

### 3.3 ARM SMP 启动

**位置**: `xen/xen/arch/arm/setup.c:891`

```891:902:xen/xen/arch/arm/setup.c
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
    /* TODO: smp_cpus_done(); */
```

**SMP 初始化** (`xen/xen/arch/arm/smpboot.c:253`):

```253:276:xen/xen/arch/arm/smpboot.c
void __init smp_init_cpus(void)
{
    int rc;

    /* initialize PSCI and set a global variable */
    psci_init();

    if ( (rc = arch_smp_init()) < 0 )
    {
        printk(XENLOG_WARNING "SMP init failed (%d)\n"
               "Using only 1 CPU\n", rc);
        return;
    }

    if ( acpi_disabled )
        dt_smp_init_cpus();
    else
        acpi_smp_init_cpus();

    if ( opt_hmp_unsafe )
        warning_add("WARNING: HMP COMPUTING HAS BEEN ENABLED.\n"
                    "It has implications on the security and stability of the system,\n"
                    "unless the cpu affinity of all domains is specified.\n");
}
```

**关键步骤**:
1. **PSCI 初始化**: 初始化电源状态协调接口（PSCI）
2. **架构特定初始化**: 调用 `arch_smp_init()`
3. **CPU 发现**: 通过 Device Tree 或 ACPI 发现 CPU
4. **HMP 警告**: 如果启用异构多处理（HMP），发出警告

**辅助 CPU 启动** (`xen/xen/arch/arm/smpboot.c:307`):

```307:389:xen/xen/arch/arm/smpboot.c
void start_secondary(void)
{
    unsigned int cpuid = init_data.cpuid;

    memset(get_cpu_info(), 0, sizeof (struct cpu_info));

    set_processor_id(cpuid);

    identify_cpu(&current_cpu_data);
    processor_setup();

    init_traps();

    /*
     * Currently Xen assumes the platform has only one kind of CPUs.
     * This assumption does not hold on big.LITTLE platform and may
     * result to instability and insecure platform (unless cpu affinity
     * is manually specified for all domains). Better to park them for
     * now.
     */
    if ( current_cpu_data.midr.bits != system_cpuinfo.midr.bits )
    {
        if ( !opt_hmp_unsafe )
        {
            printk(XENLOG_ERR
                   "CPU%u MIDR (0x%"PRIregister") does not match boot CPU MIDR (0x%"PRIregister"),\n"
                   XENLOG_ERR "disable cpu (see big.LITTLE.txt under docs/).\n",
                   smp_processor_id(), current_cpu_data.midr.bits,
                   system_cpuinfo.midr.bits);
            stop_cpu();
        }
        else
        {
            printk(XENLOG_ERR
                   "CPU%u MIDR (0x%"PRIregister") does not match boot CPU MIDR (0x%"PRIregister"),\n"
                   XENLOG_ERR "hmp-unsafe turned on so tainting Xen and keep core on!!\n",
                   smp_processor_id(), current_cpu_data.midr.bits,
                   system_cpuinfo.midr.bits);
            add_taint(TAINT_CPU_OUT_OF_SPEC);
         }
    }

    if ( dcache_line_bytes != read_dcache_line_bytes() )
    {
        printk(XENLOG_ERR "CPU%u dcache line size (%zu) does not match the boot CPU (%zu)\n",
               smp_processor_id(), read_dcache_line_bytes(),
               dcache_line_bytes);
        stop_cpu();
    }

    /*
     * system features must be updated only if we do not stop the core or
     * we might disable features due to a non used core (for example when
     * booting on big cores on a big.LITTLE system with hmp_unsafe)
     */
    update_system_features(&current_cpu_data);

    gic_init_secondary_cpu();

    set_current(idle_vcpu[cpuid]);

    /* Run local notifiers */
    notify_cpu_starting(cpuid);
    /*
     * Ensure that previous writes are visible before marking the cpu as
     * online.
     */
    smp_wmb();

    /* Now report this CPU is up */
    cpumask_set_cpu(cpuid, &cpu_online_map);

    local_irq_enable();

    /*
     * Calling request_irq() after local_irq_enable() on secondary cores
     * will make sure the assertion condition in alloc_xenheap_pages(),
     * i.e. !in_irq && local_irq_enabled() is satisfied.
     */
    init_maintenance_interrupt();
    init_timer_interrupt();

    local_abort_enable();
```

**关键操作**:
1. **CPU 识别**: 识别 CPU 类型和特性
2. **CPU 兼容性检查**: 检查 CPU 是否与引导 CPU 兼容（big.LITTLE 检查）
3. **Cache 检查**: 检查缓存行大小是否匹配
4. **GIC 初始化**: 初始化通用中断控制器
5. **设置当前 vCPU**: 设置为空闲 vCPU
6. **标记在线**: 将 CPU 添加到 `cpu_online_map`
7. **启用中断**: 启用本地中断和异常

### 3.4 CPU 亲和性

**vCPU 调度初始化** (`xen/xen/common/sched/core.c:539`):

```539:638:xen/xen/common/sched/core.c
int sched_init_vcpu(struct vcpu *v)
{
    const struct domain *d = v->domain;
    struct sched_unit *unit;
    unsigned int processor;

    if ( (unit = sched_alloc_unit(v)) == NULL )
        return 1;

    if ( is_idle_domain(d) )
        processor = v->vcpu_id;
    else
        processor = sched_select_initial_cpu(v);

    /* Initialise the per-vcpu timers. */
    spin_lock_init(&v->periodic_timer_lock);
    init_timer(&v->periodic_timer, vcpu_periodic_timer_fn, v, processor);
    init_timer(&v->singleshot_timer, vcpu_singleshot_timer_fn, v, processor);
    init_timer(&v->poll_timer, poll_timer_fn, v, processor);

    /* If this is not the first vcpu of the unit we are done. */
    if ( unit->priv != NULL )
    {
        v->processor = processor;
        return 0;
    }

    rcu_read_lock(&sched_res_rculock);

    /* The first vcpu of an unit can be set via sched_set_res(). */
    sched_set_res(unit, get_sched_res(processor));

    unit->priv = sched_alloc_udata(dom_scheduler(d), unit, d->sched_priv);
    if ( unit->priv == NULL )
    {
        sched_free_unit(unit, v);
        rcu_read_unlock(&sched_res_rculock);
        return 1;
    }

    if ( is_idle_domain(d) )
    {
        /* Idle vCPUs are always pinned onto their respective pCPUs */
        sched_set_affinity(unit, cpumask_of(processor), &cpumask_all);
    }
    else if ( pv_shim && v->vcpu_id == 0 )
    {
        /*
         * PV-shim: vcpus are pinned 1:1. Initially only 1 cpu is online,
         * others will be dealt with when onlining them. This avoids pinning
         * a vcpu to a not yet online cpu here.
         */
        sched_set_affinity(unit, cpumask_of(0), cpumask_of(0));
    }
    else if ( d->domain_id == 0 && opt_dom0_vcpus_pin )
    {
        /*
         * If dom0_vcpus_pin is specified, dom0 vCPUs are pinned 1:1 to
         * their respective pCPUs too.
         */
        sched_set_affinity(unit, cpumask_of(processor), &cpumask_all);
    }
#ifdef CONFIG_X86
    else if ( d->domain_id == 0 )
    {
        /*
         * In absence of dom0_vcpus_pin instead, the hard and soft affinity of
         * dom0 is controlled by the (x86 only) dom0_nodes parameter. At this
         * point it has been parsed and decoded into the dom0_cpus mask.
         *
         * Note that we always honor what user explicitly requested, for both
         * hard and soft affinity, without doing any dynamic computation of
         * either of them.
         */
        if ( !dom0_affinity_relaxed )
            sched_set_affinity(unit, &dom0_cpus, &cpumask_all);
        else
            sched_set_affinity(unit, &cpumask_all, &dom0_cpus);
    }
#endif
    else
        sched_set_affinity(unit, &cpumask_all, &cpumask_all);

    /* Idle VCPUs are scheduled immediately, so don't put them in runqueue. */
    if ( is_idle_domain(d) )
    {
        get_sched_res(v->processor)->curr = unit;
        get_sched_res(v->processor)->sched_unit_idle = unit;
        v->is_running = true;
        unit->is_running = true;
        unit->state_entry_time = NOW();
    }
    else
    {
        sched_insert_unit(dom_scheduler(d), unit);
    }

    rcu_read_unlock(&sched_res_rculock);

    return 0;
```

**亲和性设置**:
- **空闲域**: vCPU 固定到对应的物理 CPU
- **PV-shim**: vCPU 1:1 固定到物理 CPU
- **Domain 0**: 根据配置固定或使用 NUMA 节点
- **普通域**: 默认可以使用所有 CPU

## 四、总结

### 4.1 vCPU 生命周期

```
创建 (vcpu_create)
    |
    v
离线 (RUNSTATE_offline, _VPF_down)
    |
    v
初始化 (VCPUOP_initialise)
    |
    v
上线 (VCPUOP_up)
    |
    v
可运行 (RUNSTATE_runnable)
    |
    v
运行 (RUNSTATE_running)
    |
    v
下线 (VCPUOP_down)
    |
    v
销毁 (vcpu_destroy)
```

### 4.2 Domain 启动流程

```
Domain 创建
    |
    v
暂停状态 (controller_pause_count = 1)
    |
    v
配置和初始化
    |
    v
VCPUOP_up (使 vCPU 可运行)
    |
    v
domain_unpause_by_systemcontroller()
    |
    v
Domain 运行 (creation_finished = true)
```

### 4.3 SMP 支持

**CPU 状态转换**:
```
可能 (cpu_possible_map)
    |
    v
存在 (cpu_present_map)
    |
    v
上线 (cpu_up)
    |
    v
在线 (cpu_online_map)
```

**关键机制**:
- **CPU 发现**: 通过 ACPI/Device Tree 发现 CPU
- **CPU 上线**: 通过 IPI/PSCI 唤醒辅助 CPU
- **CPU 亲和性**: vCPU 可以固定到特定 CPU 或使用所有 CPU
- **NUMA 支持**: 支持 NUMA 拓扑和 CPU 到节点的映射

## 五、关键概念

### 5.1 调度单元 (Scheduling Unit)

- **概念**: 一个调度单元可以包含多个 vCPU（用于 SMT）
- **用途**: 调度器以调度单元为单位进行调度
- **关系**: 一个 Domain 可以有多个调度单元，每个调度单元可以有多个 vCPU

### 5.2 暂停机制

- **引用计数**: Domain 使用引用计数管理暂停状态
- **同步/异步**: 支持同步和异步暂停
- **控制器暂停**: 工具栈使用独立的暂停计数

### 5.3 CPU 热插拔

- **上线**: `cpu_up()` - 启动 CPU
- **下线**: `cpu_down()` - 停止 CPU
- **停车**: 超过最大 CPU 数时，CPU 会被停车（parked）

## 六、参考文档

- [Xen 启动流程](./xen-startup.md)
- [调度器架构](./scheduler-architecture.md) (如果存在)
- [Domain 0 的本质](./domain0-essence.md)
