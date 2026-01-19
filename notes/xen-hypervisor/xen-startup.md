# Xen Hypervisor 启动流程分析

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/arch/x86/setup.c` - x86 架构启动代码
- `xen/xen/arch/arm/setup.c` - ARM 架构启动代码
- `xen/xen/arch/x86/pv/dom0_build.c` - x86 Domain 0 构建
- `xen/xen/arch/arm/domain_build.c` - ARM Domain 0 构建
- `xen/xen/common/domain.c` - 通用 Domain 管理
- `xen/xen/common/kernel.c` - 通用内核功能

## 概述

Xen Hypervisor 的启动流程是理解整个系统的基础。本文档详细分析了 Xen 从启动到 Domain 0 运行的完整流程，涵盖 x86 和 ARM 两个主要架构。

**注意**: 本文档提供启动流程的总览。更详细的架构特定信息请参考：
- [ARM 架构启动流程](./arm-startup-flow.md)
- [ARM 架构特定启动流程](./arm-arch-specific-startup.md)
- [通用启动流程](./common-startup-flow.md)

## 一、启动入口

### 1.1 x86 架构

**主入口函数**: `__start_xen()`
**位置**: `xen/xen/arch/x86/setup.c:972`

```972:972:xen/xen/arch/x86/setup.c
void __init noreturn __start_xen(unsigned long mbi_p)
```

**参数**:
- `mbi_p`: Multiboot 信息结构的物理地址

**启动协议**:
- **Multiboot 1/2**: 传统 BIOS 启动
- **EFI Multiboot 2**: UEFI 环境启动
- **PVH (Paravirtualized Hardware)**: 虚拟化环境启动

### 1.2 ARM 架构

**主入口函数**: `start_xen()`
**位置**: `xen/xen/arch/arm/setup.c:763`

```763:764:xen/xen/arch/arm/setup.c
void __init start_xen(unsigned long boot_phys_offset,
                      unsigned long fdt_paddr)
```

**参数**:
- `boot_phys_offset`: 物理地址偏移
- `fdt_paddr`: Device Tree 的物理地址

**启动要求**:
- **异常级别**: 必须在 EL2 (Hypervisor 模式)
- **MMU**: 关闭
- **D-cache**: 关闭
- **I-cache**: 开启或关闭都可以

## 二、x86 架构启动流程

### 2.1 阶段 1: 关键区域初始化 (993-1013)

```993:1013:xen/xen/arch/x86/setup.c
    /* Critical region without IDT or TSS.  Any fault is deadly! */

    init_shadow_spec_ctrl_state();

    percpu_init_areas();

    init_idt_traps();
    load_system_tables();

    smp_prepare_boot_cpu();
    sort_exception_tables();

    setup_virtual_regions(__start___ex_table, __stop___ex_table);

    /* Full exception support from here on in. */

    rdmsrl(MSR_EFER, this_cpu(efer));
    asm volatile ( "mov %%cr4,%0" : "=r" (info->cr4) );

    /* Enable NMIs.  Our loader (e.g. Tboot) may have left them disabled. */
    enable_nmis();
```

**关键操作**:
- **Shadow Spec Control**: 初始化影子规范控制状态（安全特性）
- **Per-CPU 区域**: 初始化每个 CPU 的私有数据区域
- **IDT 和系统表**: 设置中断描述符表和系统表
- **异常表**: 排序异常处理表
- **虚拟区域**: 设置虚拟内存区域
- **CPU 寄存器**: 读取 EFER 和 CR4 寄存器
- **NMI**: 启用不可屏蔽中断

### 2.2 阶段 2: 启动协议处理 (1015-1044)

```1015:1044:xen/xen/arch/x86/setup.c
    if ( pvh_boot )
    {
        ASSERT(mbi_p == 0);
        pvh_init(&mbi, &mod);
    }
    else
    {
        mbi = __va(mbi_p);
        mod = __va(mbi->mods_addr);
    }

    loader = (mbi->flags & MBI_LOADERNAME)
        ? (char *)__va(mbi->boot_loader_name) : "unknown";

    /* Parse the command-line options. */
    cmdline = cmdline_cook((mbi->flags & MBI_CMDLINE) ?
                           __va(mbi->cmdline) : NULL,
                           loader);
    if ( (kextra = strstr(cmdline, " -- ")) != NULL )
    {
        /*
         * Options after ' -- ' separator belong to dom0.
         *  1. Orphan dom0's options from Xen's command line.
         *  2. Skip all but final leading space from dom0's options.
         */
        *kextra = '\0';
        kextra += 3;
        while ( kextra[1] == ' ' ) kextra++;
    }
    cmdline_parse(cmdline);
```

**关键操作**:
- **PVH 启动**: 处理 Paravirtualized Hardware 启动协议
- **Multiboot 信息**: 解析 Multiboot 信息结构
- **命令行解析**: 解析 Xen 和 Domain 0 的命令行参数
- **模块信息**: 获取启动模块信息（Domain 0 内核、initrd 等）

### 2.3 阶段 3: 控制台和硬件探测 (1054-1124)

```1054:1124:xen/xen/arch/x86/setup.c
    hypervisor_name = hypervisor_probe();

    parse_video_info();

    /* We initialise the serial devices very early so we can get debugging. */
    ns16550.io_base = 0x3f8;
    ns16550.irq     = 4;
    ns16550_init(0, &ns16550);
    ns16550.io_base = 0x2f8;
    ns16550.irq     = 3;
    ns16550_init(1, &ns16550);
    ehci_dbgp_init();
    xhci_dbc_uart_init();
    console_init_preirq();
```

**关键操作**:
- **Hypervisor 探测**: 检测是否在虚拟化环境中运行
- **视频信息**: 解析 VGA/视频信息
- **串口初始化**: 初始化串口设备（用于调试输出）
- **控制台初始化**: 初始化控制台（IRQ 之前）

### 2.4 阶段 4: 内存映射初始化 (1132-1349)

```1132:1349:xen/xen/arch/x86/setup.c
    /* Check that we have at least one Multiboot module. */
    if ( !(mbi->flags & MBI_MODULES) || (mbi->mods_count == 0) )
        panic("dom0 kernel not specified. Check bootloader configuration\n");

    /* Check that we don't have a silly number of modules. */
    if ( mbi->mods_count > sizeof(module_map) * 8 )
    {
        mbi->mods_count = sizeof(module_map) * 8;
        printk("Excessive multiboot modules - using the first %u only\n",
               mbi->mods_count);
    }

    bitmap_fill(module_map, mbi->mods_count);
    __clear_bit(0, module_map); /* Dom0 kernel is always first */
```

**关键操作**:
- **模块检查**: 确保至少有一个模块（Domain 0 内核）
- **E820 内存映射**: 解析 BIOS/UEFI 提供的物理内存映射
- **内存类型**: 区分 RAM、保留、ACPI、NVS 等内存类型
- **早期 CPU 初始化**: 初始化 CPU 特性
- **微码更新**: 加载 CPU 微码更新

### 2.5 阶段 5: 内存管理初始化 (1716-1760)

```1716:1760:xen/xen/arch/x86/setup.c
    init_frametable();

    if ( !acpi_boot_table_init_done )
        acpi_boot_table_init();

    acpi_numa_init();

    numa_initmem_init(0, raw_max_page);

    if ( max_page - 1 > virt_to_mfn(HYPERVISOR_VIRT_END - 1) )
    {
        unsigned long limit = virt_to_mfn(HYPERVISOR_VIRT_END - 1);
        uint64_t mask = PAGE_SIZE - 1;

        if ( !highmem_start )
            xenheap_max_mfn(limit);

        end_boot_allocator();

        /* Pass the remaining memory to the allocator. */
        for ( i = 0; i < boot_e820.nr_map; i++ )
        {
            uint64_t s, e;

            if ( boot_e820.map[i].type != E820_RAM )
                continue;
            s = (boot_e820.map[i].addr + mask) & ~mask;
            e = (boot_e820.map[i].addr + boot_e820.map[i].size) & ~mask;
            if ( PFN_DOWN(e) <= limit )
                continue;
            if ( PFN_DOWN(s) <= limit )
                s = pfn_to_paddr(limit + 1);
            init_domheap_pages(s, e);
        }
    }
    else
        end_boot_allocator();

    system_state = SYS_STATE_boot;
    /*
     * No calls involving ACPI code should go between the setting of
     * SYS_STATE_boot and vm_init() (or else acpi_os_{,un}map_memory()
     * will break).
     */
    vm_init();
```

**关键操作**:
- **页帧表初始化**: 初始化物理页帧管理表
- **ACPI 初始化**: 解析 ACPI 表（如果使用 ACPI）
- **NUMA 初始化**: 初始化 NUMA（非统一内存访问）支持
- **内存分配器**: 结束启动分配器，切换到正常分配器
- **系统状态**: 切换到 `SYS_STATE_boot`
- **虚拟化初始化**: 初始化虚拟化子系统

### 2.6 阶段 6: 虚拟化和分页初始化 (1760-1793)

```1760:1793:xen/xen/arch/x86/setup.c
    vm_init();

    bsp_stack = cpu_alloc_stack(0);
    if ( !bsp_stack )
        panic("No memory for BSP stack\n");

    console_init_ring();
    vesa_init();

    tasklet_subsys_init();

    paging_init();

    tboot_probe();

    open_softirq(NEW_TLBFLUSH_CLOCK_PERIOD_SOFTIRQ, new_tlbflush_clock_period);

    if ( opt_watchdog )
        nmi_watchdog = NMI_LOCAL_APIC;

    find_smp_config();

    dmi_scan_machine();

    mmio_ro_ranges = rangeset_new(NULL, "r/o mmio ranges",
                                  RANGESETF_prettyprint_hex);

    xsm_multiboot_init(module_map, mbi);

    /*
     * IOMMU-related ACPI table parsing may require some of the system domains
     * to be usable, e.g. for pci_hide_device()'s use of dom_xen.
     */
    setup_system_domains();
```

**关键操作**:
- **BSP 栈**: 为引导处理器分配栈
- **控制台环**: 初始化控制台环形缓冲区
- **任务子系统**: 初始化任务子系统
- **分页初始化**: 初始化分页机制
- **SMP 配置**: 查找多处理器配置
- **系统域**: 设置系统域（dom_xen、dom_io 等）

### 2.7 阶段 7: IOMMU 和中断初始化 (1795-1900)

**关键操作**:
- **IOMMU 初始化**: 初始化 I/O 内存管理单元
- **APIC 初始化**: 初始化高级可编程中断控制器
- **中断路由**: 设置中断路由

### 2.8 阶段 8: Domain 0 创建 (2014-2020)

```871:875:xen/xen/arch/x86/setup.c
static struct domain *__init create_dom0(const module_t *image,
                                         unsigned long headroom,
                                         module_t *initrd, const char *kextra,
                                         const char *loader)
```

```2014:2014:xen/xen/arch/x86/setup.c
    dom0 = create_dom0(mod, modules_headroom,
```

**关键操作**:
- **Domain 创建**: 调用 `domain_create()` 创建 Domain 0
- **Domain 构建**: 调用 `construct_dom0()` 构建 Domain 0
- **内核加载**: 加载 Domain 0 内核镜像
- **Initrd 加载**: 加载初始 RAM 磁盘（如果提供）

### 2.9 阶段 9: 系统激活 (2100+)

**关键操作**:
- **丢弃初始模块**: 释放启动时使用的临时内存
- **堆内存初始化**: 初始化堆内存分配器
- **XSM 激活**: 将 XSM 切换到系统激活状态
- **系统状态**: 设置为 `SYS_STATE_active`
- **取消暂停域**: 取消暂停所有域，允许运行

## 三、ARM 架构启动流程

### 3.1 阶段 1: 早期初始化 (763-803)

```763:803:xen/xen/arch/arm/setup.c
void __init start_xen(unsigned long boot_phys_offset,
                      unsigned long fdt_paddr)
{
    size_t fdt_size;
    const char *cmdline;
    struct bootmodule *xen_bootmodule;
    struct domain *d;
    int rc, i;

    dcache_line_bytes = read_dcache_line_bytes();

    percpu_init_areas();
    set_processor_id(0); /* needed early, for smp_processor_id() */

    setup_virtual_regions(NULL, NULL);
    /* Initialize traps early allow us to get backtrace when an error occurred */
    init_traps();

    setup_pagetables(boot_phys_offset);

    smp_clear_cpu_maps();

    device_tree_flattened = early_fdt_map(fdt_paddr);
    if ( !device_tree_flattened )
        panic("Invalid device tree blob at physical address %#lx.\n"
              "The DTB must be 8-byte aligned and must not exceed 2 MB in size.\n\n"
              "Please check your bootloader.\n",
              fdt_paddr);

    /* Register Xen's load address as a boot module. */
    xen_bootmodule = add_boot_module(BOOTMOD_XEN,
                             virt_to_maddr(_start),
                             (paddr_t)(uintptr_t)(_end - _start), false);
    BUG_ON(!xen_bootmodule);

    fdt_size = boot_fdt_info(device_tree_flattened, fdt_paddr);

    cmdline = boot_fdt_cmdline(device_tree_flattened);
    printk("Command line: %s\n", cmdline);
    cmdline_parse(cmdline);
```

**关键操作**:
- **Cache 行大小**: 读取数据缓存行大小
- **Per-CPU 区域**: 初始化每个 CPU 的私有数据
- **异常处理**: 初始化异常处理（陷阱）
- **页表设置**: 设置页表
- **Device Tree**: 映射和解析设备树
- **命令行解析**: 解析启动命令行参数

### 3.2 阶段 2: 内存管理 (804-815)

```804:815:xen/xen/arch/arm/setup.c
    setup_mm();

    /* Parse the ACPI tables for possible boot-time configuration */
    acpi_boot_table_init();

    end_boot_allocator();

    /*
     * The memory subsystem has been initialized, we can now switch from
     * early_boot -> boot.
     */
    system_state = SYS_STATE_boot;
```

**关键操作**:
- **内存管理**: 初始化内存管理子系统
- **ACPI**: 解析 ACPI 表（如果使用 ACPI）
- **启动分配器**: 结束启动阶段的临时分配器
- **系统状态**: 切换到 `SYS_STATE_boot`

### 3.3 阶段 3: 虚拟化和设备树 (817-829)

```817:829:xen/xen/arch/arm/setup.c
    vm_init();

    if ( acpi_disabled )
    {
        printk("Booting using Device Tree\n");
        device_tree_flattened = relocate_fdt(fdt_paddr, fdt_size);
        dt_unflatten_host_device_tree();
    }
    else
    {
        printk("Booting using ACPI\n");
        device_tree_flattened = NULL;
    }
```

**关键操作**:
- **虚拟化初始化**: 初始化虚拟化子系统
- **Device Tree**: 展开设备树（如果使用 DT）
- **ACPI**: 如果使用 ACPI，清空 Device Tree

### 3.4 阶段 4: 中断和平台初始化 (831-860)

```831:860:xen/xen/arch/arm/setup.c
    init_IRQ();

    platform_init();

    preinit_xen_time();

    gic_preinit();

    arm_uart_init();
    console_init_preirq();
    console_init_ring();

    processor_id();

    smp_init_cpus();
    nr_cpu_ids = smp_get_max_cpus();
    printk(XENLOG_INFO "SMP: Allowing %u CPUs\n", nr_cpu_ids);

    /*
     * Some errata relies on SMCCC version which is detected by psci_init()
     * (called from smp_init_cpus()).
     */
    check_local_cpu_errata();

    check_local_cpu_features();

    init_xen_time();

    gic_init();
```

**关键操作**:
- **中断初始化**: 初始化中断子系统
- **平台初始化**: 初始化平台特定功能
- **GIC**: 初始化通用中断控制器
- **UART**: 初始化串口
- **SMP**: 初始化多处理器支持
- **CPU 特性**: 检查 CPU 特性和错误修复

### 3.5 阶段 5: 调度器和系统域 (861-878)

```861:878:xen/xen/arch/arm/setup.c
    tasklet_subsys_init();

    if ( xsm_dt_init() != 1 )
        warning_add("WARNING: SILO mode is not enabled.\n"
                    "It has implications on the security of the system,\n"
                    "unless the communications have been forbidden between\n"
                    "untrusted domains.\n");

    init_maintenance_interrupt();
    init_timer_interrupt();

    timer_init();

    init_idle_domain();

    rcu_init();

    setup_system_domains();
```

**关键操作**:
- **任务子系统**: 初始化任务子系统
- **XSM**: 初始化 Xen 安全模块
- **中断**: 初始化维护和定时器中断
- **空闲域**: 初始化空闲域
- **RCU**: 初始化 RCU（Read-Copy-Update）机制
- **系统域**: 设置系统域

### 3.6 阶段 6: 多核启动 (880-901)

```880:901:xen/xen/arch/arm/setup.c
    local_irq_enable();
    local_abort_enable();

    smp_prepare_cpus();

    initialize_keytable();

    console_init_postirq();

    do_presmp_initcalls();

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

**关键操作**:
- **中断启用**: 启用本地中断和异常
- **SMP 准备**: 准备多处理器环境
- **初始化调用**: 执行 SMP 之前的初始化调用
- **CPU 上线**: 启动所有可用的 CPU

### 3.7 阶段 7: IOMMU 和分页 (903-916)

```903:916:xen/xen/arch/arm/setup.c
    /*
     * The IOMMU subsystem must be initialized before P2M as we need
     * to gather requirements regarding the maximum IPA bits supported by
     * each IOMMU device.
     */
    rc = iommu_setup();
    if ( rc )
        panic("IOMMU setup failed: %d\n", rc);

    setup_virt_paging();
```

**关键操作**:
- **IOMMU 初始化**: 初始化 I/O 内存管理单元（必须在 P2M 之前）
- **虚拟分页**: 设置虚拟分页机制

### 3.8 阶段 8: Domain 0 创建 (918-938)

```918:938:xen/xen/arch/arm/setup.c
    do_initcalls();

    /*
     * It needs to be called after do_initcalls to be able to use
     * stop_machine (tasklets initialized via an initcall).
     */
    apply_alternatives_all();
    enable_errata_workarounds();
    enable_cpu_features();

    /* Create initial domain 0. */
    if ( !is_dom0less_mode() )
        create_dom0();
    else
        printk(XENLOG_INFO "Xen dom0less mode detected\n");

    if ( acpi_disabled )
    {
        create_domUs();
        alloc_static_evtchn();
    }
```

**关键操作**:
- **初始化调用**: 执行所有初始化调用
- **CPU 替代方案**: 应用 CPU 替代方案和错误修复
- **Domain 0 创建**: 创建 Domain 0（除非是 dom0less 模式）
- **Domain U 创建**: 创建其他 Domain U（如果配置了）

### 3.9 阶段 9: 系统激活 (940-970)

```940:970:xen/xen/arch/arm/setup.c
    /*
     * This needs to be called **before** heap_init_late() so modules
     * will be scrubbed (unless suppressed).
     */
    discard_initial_modules();

    heap_init_late();

    init_trace_bufs();

    init_constructors();

    console_endboot();

    /* Hide UART from DOM0 if we're using it */
    serial_endboot();

    if ( (rc = xsm_set_system_active()) != 0 )
        panic("xsm: unable to switch to SYSTEM_ACTIVE privilege: %d\n", rc);

    system_state = SYS_STATE_active;

    for_each_domain( d )
        domain_unpause_by_systemcontroller(d);

    /* Switch on to the dynamically allocated stack for the idle vcpu
     * since the static one we're running on is about to be freed. */
    memcpy(idle_vcpu[0]->arch.cpu_info, get_cpu_info(),
           sizeof(struct cpu_info));
    switch_stack_and_jump(idle_vcpu[0]->arch.cpu_info, init_done);
```

**关键操作**:
- **丢弃模块**: 丢弃启动时使用的临时模块
- **堆内存**: 初始化堆内存分配器
- **跟踪缓冲区**: 初始化跟踪缓冲区
- **XSM 激活**: 将 XSM 切换到系统激活状态
- **系统状态**: 设置为 `SYS_STATE_active`
- **取消暂停**: 取消暂停所有域
- **栈切换**: 切换到动态分配的栈并跳转到 `init_done()`

## 四、系统状态机

Xen 使用系统状态机管理启动过程：

```29:29:xen/xen/common/kernel.c
enum system_state system_state = SYS_STATE_early_boot;
```

**状态转换**:

1. **`SYS_STATE_early_boot`** (初始状态)
   - 早期启动阶段
   - 内存分配器尚未完全初始化
   - 只能使用启动分配器

2. **`SYS_STATE_boot`** (启动阶段)
   - 内存管理已初始化
   - 可以使用正常的内存分配器
   - 虚拟化子系统已初始化

3. **`SYS_STATE_active`** (激活状态)
   - 系统完全激活
   - Domain 0 已创建
   - 所有域可以运行

## 五、Domain 0 创建流程

### 5.1 Domain 创建函数

**位置**: `xen/xen/common/domain.c:583`

```583:585:xen/xen/common/domain.c
struct domain *domain_create(domid_t domid,
                             struct xen_domctl_createdomain *config,
                             unsigned int flags)
```

**关键步骤**:
1. **分配 Domain 结构**: 分配 Domain 数据结构
2. **设置 Domain ID**: 设置 Domain ID（Domain 0 的 ID 为 0）
3. **设置特权标志**: Domain 0 使用 `CDF_privileged` 标志
4. **初始化子系统**:
   - 事件通道 (Event Channel)
   - 授权表 (Grant Table)
   - 调度器
   - XSM
5. **添加到 Domain 列表**: 添加到全局 Domain 列表和哈希表

### 5.2 Domain 0 构建

**x86 架构**: `xen/xen/arch/x86/pv/dom0_build.c`
**ARM 架构**: `xen/xen/arch/arm/domain_build.c`

**关键操作**:
1. **内存分配**: 为 Domain 0 分配内存
2. **页表设置**: 设置 Domain 0 的页表
3. **内核加载**: 加载 Domain 0 内核镜像
4. **Initrd 加载**: 加载初始 RAM 磁盘（如果提供）
5. **启动信息**: 准备启动信息结构
6. **VCPU 创建**: 创建 Domain 0 的虚拟 CPU

### 5.3 create_dom0 和 construct_dom0 详细流程

#### 5.3.1 create_dom0 函数概述

`create_dom0` 是创建 Domain 0 的入口函数，负责：
1. 配置 Domain 0 的创建参数
2. 调用 `domain_create()` 创建 Domain 结构
3. 初始化 Domain 0 的 VCPU 0
4. 调用 `construct_dom0()` 完成 Domain 0 的构建

#### 5.3.2 x86 架构的 create_dom0

**位置**: `xen/xen/arch/x86/setup.c:871`

```871:966:xen/xen/arch/x86/setup.c
static struct domain *__init create_dom0(const module_t *image,
                                         unsigned long headroom,
                                         module_t *initrd, const char *kextra,
                                         const char *loader)
{
    struct xen_domctl_createdomain dom0_cfg = {
        .flags = IS_ENABLED(CONFIG_TBOOT) ? XEN_DOMCTL_CDF_s3_integrity : 0,
        .max_evtchn_port = -1,
        .max_grant_frames = -1,
        .max_maptrack_frames = -1,
        .grant_opts = XEN_DOMCTL_GRANT_version(opt_gnttab_max_version),
        .max_vcpus = dom0_max_vcpus(),
        .arch = {
            .misc_flags = opt_dom0_msr_relaxed ? XEN_X86_MSR_RELAXED : 0,
        },
    };
    // ... 配置 PVH 模式（如果启用）
    // ... 配置 IOMMU（如果启用）
    // ... 调用 domain_create()
    // ... 初始化 CPUID 策略
    // ... 分配 VCPU 0
    // ... 处理命令行参数
    // ... 调用 construct_dom0()
}
```

**流程步骤**:

1. **配置 Domain 0 参数** (876-886)
   - 设置基本标志（如 TBOOT S3 完整性）
   - 配置事件通道、授权表、映射跟踪帧的限制
   - 设置最大 VCPU 数量
   - 配置架构特定选项（如 MSR 放宽）

2. **PVH 模式配置** (891-899)
   - 如果启用 `opt_dom0_pvh`，设置 HVM 和 HAP 标志
   - 配置需要模拟的设备（LAPIC、IOAPIC、vPCI）

3. **IOMMU 配置** (901-902)
   - 如果 IOMMU 启用，添加 `XEN_DOMCTL_CDF_iommu` 标志

4. **创建 Domain** (905-908)
   - 获取初始 Domain ID（通常是 0）
   - 调用 `domain_create()` 创建 Domain 结构
   - 设置特权标志（除非是 pvshim 模式）

5. **初始化 CPUID 策略** (910)
   - 调用 `init_dom0_cpuid_policy()` 设置 Domain 0 的 CPUID 策略

6. **分配 VCPU 0** (912-913)
   - 调用 `alloc_dom0_vcpu0()` 为 Domain 0 创建第一个 VCPU

7. **处理命令行参数** (915-943)
   - 从镜像中提取命令行
   - 合并额外的命令行参数（kextra）
   - 添加 ACPI 相关参数
   - 处理 IOAPIC 设置

8. **临时禁用 SMAP** (946-954)
   - 如果 CPU 支持 SMAP，临时禁用以允许用户空间访问
   - 这简化了 `construct_dom0()` 中的 `copy_from_user()` 调用

9. **调用 construct_dom0** (956-957)
   - 调用 `construct_dom0()` 完成 Domain 0 的构建

10. **恢复 SMAP** (959-963)
    - 重新启用 SMAP 保护

#### 5.3.3 ARM 架构的 create_dom0

**位置**: `xen/xen/arch/arm/domain_build.c:3882`

```3882:3929:xen/xen/arch/arm/domain_build.c
void __init create_dom0(void)
{
    struct domain *dom0;
    struct xen_domctl_createdomain dom0_cfg = {
        .flags = XEN_DOMCTL_CDF_hvm | XEN_DOMCTL_CDF_hap,
        .max_evtchn_port = -1,
        .max_grant_frames = gnttab_dom0_frames(),
        .max_maptrack_frames = -1,
        .grant_opts = XEN_DOMCTL_GRANT_version(opt_gnttab_max_version),
    };
    // ... 配置 GIC
    // ... 配置 SVE（如果启用）
    // ... 调用 domain_create()
    // ... 分配 VCPU 0
    // ... 调用 construct_dom0()
}
```

**流程步骤**:

1. **配置 Domain 0 参数** (3885-3891)
   - 设置 HVM 和 HAP 标志（ARM 上 Domain 0 总是 HVM）
   - 配置授权表帧数

2. **配置 GIC** (3894-3902)
   - 设置 GIC 版本为原生（`XEN_DOMCTL_CONFIG_GIC_NATIVE`）
   - 配置 SPI（共享外设中断）数量
   - 设置 TEE 类型

3. **配置 VCPU** (3904)
   - 设置最大 VCPU 数量

4. **IOMMU 配置** (3906-3907)
   - 如果 IOMMU 启用，添加相应标志

5. **SVE 配置** (3909-3917)
   - 如果启用 SVE（可扩展向量扩展），配置向量长度

6. **创建 Domain** (3919-3921)
   - 调用 `domain_create()` 创建 Domain 0
   - 设置特权标志和直接映射标志

7. **分配 VCPU 0** (3923-3924)
   - 为 Domain 0 创建第一个 VCPU

8. **调用 construct_dom0** (3926-3928)
   - 调用 `construct_dom0()` 完成 Domain 0 的构建

#### 5.3.4 construct_dom0 函数概述

`construct_dom0` 是构建 Domain 0 的核心函数，负责：
1. 根据 Domain 类型（PV/PVH/HVM）调用相应的构建函数
2. 完成内存分配、页表设置、内核加载等具体工作

#### 5.3.5 x86 架构的 construct_dom0

**位置**: `xen/xen/arch/x86/dom0_build.c:588`

```588:615:xen/xen/arch/x86/dom0_build.c
int __init construct_dom0(struct domain *d, const module_t *image,
                          unsigned long image_headroom, module_t *initrd,
                          const char *cmdline)
{
    // ... 参数检查
    // ... 处理待处理的软中断
    // ... 根据 Domain 类型调用相应的构建函数
    if ( is_hvm_domain(d) )
        rc = dom0_construct_pvh(d, image, image_headroom, initrd, cmdline);
    else if ( is_pv_domain(d) )
        rc = dom0_construct_pv(d, image, image_headroom, initrd, cmdline);
    // ... 验证 VCPU 0 已初始化
}
```

**流程步骤**:

1. **参数验证** (594-597)
   - 验证 Domain ID 为 0（除非是 pvshim）
   - 验证 VCPU 0 存在且未初始化

2. **处理软中断** (599)
   - 处理待处理的软中断

3. **根据 Domain 类型构建** (601-606)
   - **HVM Domain**: 调用 `dom0_construct_pvh()` 构建 PVH Domain 0
   - **PV Domain**: 调用 `dom0_construct_pv()` 构建 PV Domain 0

4. **验证构建结果** (611-612)
   - 验证 VCPU 0 已成功初始化

**dom0_construct_pv 流程** (`xen/xen/arch/x86/pv/dom0_build.c:357`):

1. **ELF 解析**: 解析 Domain 0 内核的 ELF 格式
2. **内存布局计算**: 计算虚拟内存布局（内核、initrd、页表等）
3. **页表分配**: 分配和设置页表
4. **内存分配**: 为 Domain 0 分配物理内存
5. **内核加载**: 将内核镜像加载到 Domain 0 的内存中
6. **Initrd 加载**: 加载初始 RAM 磁盘（如果提供）
7. **启动信息设置**: 准备 `start_info` 结构
8. **VCPU 初始化**: 初始化 VCPU 0 的寄存器状态

**dom0_construct_pvh 流程** (`xen/xen/arch/x86/hvm/dom0_build.c:1178`):

1. **权限设置**: 设置 Domain 0 的 I/O 权限
2. **MMCFG 初始化**: 初始化 PCIe MMCFG 区域
3. **P2M 初始化**: 初始化物理到机器的映射
4. **IOMMU 初始化**: 初始化硬件 Domain 的 IOMMU
5. **P2M 填充**: 填充物理内存映射
6. **内核加载**: 加载 PVH 内核
7. **CPU 设置**: 设置 Domain 0 的 CPU 状态
8. **ACPI 设置**: 准备 ACPI 表

#### 5.3.6 ARM 架构的 construct_dom0

**位置**: `xen/xen/arch/arm/domain_build.c:3808`

```3808:3880:xen/xen/arch/arm/domain_build.c
static int __init construct_dom0(struct domain *d)
{
    struct kernel_info kinfo = {};
    // ... 内存配置
    // ... 内核探测
    // ... 内存分配
    // ... 设备映射
    // ... DTB/ACPI 准备
    // ... 调用 construct_domain()
}
```

**流程步骤**:

1. **内存配置** (3821-3833)
   - 解析 `dom0_mem` 参数（默认 512MB）
   - 设置 Domain 0 的最大页数

2. **IOMMU 初始化** (3831)
   - 初始化硬件 Domain 的 IOMMU

3. **内核探测** (3838-3840)
   - 调用 `kernel_probe()` 探测内核类型和格式

4. **内存分配** (3846-3847)
   - 调用 `allocate_memory_11()` 为 Domain 0 分配内存
   - 查找授权表区域

5. **静态共享内存** (3849-3853)
   - 如果启用，处理静态共享内存

6. **设备映射** (3856-3862)
   - 映射 GIC MMIO 和中断到 Domain 0
   - 执行平台特定的映射

7. **设备树/ACPI 准备** (3864-3874)
   - **Device Tree**: 如果使用 DT，准备 DTB
   - **ACPI**: 如果使用 ACPI，准备 ACPI 表
   - 处理 PCI 主机桥映射（如果使用 DT）

8. **构建 Domain** (3879)
   - 调用 `construct_domain()` 完成 Domain 构建

#### 5.3.7 流程对比总结

| 步骤 | x86 (PV) | x86 (PVH) | ARM |
|------|----------|-----------|-----|
| Domain 类型 | PV | HVM | HVM |
| 内核格式 | ELF | ELF | 多种格式 |
| 页表设置 | 手动设置多级页表 | 使用 P2M | 使用 P2M |
| 启动信息 | start_info | start_info | DTB/ACPI |
| 内存布局 | 固定布局 | 动态布局 | 动态布局 |
| 设备访问 | 通过 hypercall | 直接访问 | 直接访问 |

#### 5.3.8 关键数据结构

**xen_domctl_createdomain**:
- 定义 Domain 的创建参数
- 包括标志、资源限制、架构特定配置

**kernel_info** (ARM):
- 存储内核相关信息
- 包括未分配内存、Domain 指针等

**module_t** (x86):
- 描述启动模块（内核、initrd）
- 包括物理地址、大小、命令行等

### 5.4 系统切换到 Domain 0 运行的流程

#### 5.4.1 概述

Domain 0 创建后处于暂停状态，系统需要通过一系列步骤才能让 Domain 0 开始运行。这个过程涉及：
1. Domain 暂停机制
2. VCPU 初始化和上线
3. 系统状态切换
4. Domain 取消暂停
5. 调度器调度
6. 上下文切换

#### 5.4.2 Domain 0 的初始状态

**创建时的状态**:

当 Domain 0 通过 `domain_create()` 创建时：

```722:723:xen/xen/common/domain.c
        d->controller_pause_count = 1;
        atomic_inc(&d->pause_count);
```

- **`controller_pause_count = 1`**: 系统控制器暂停计数，初始为 1
- **`pause_count`**: Domain 暂停计数，初始为 1
- **VCPU 状态**: VCPU 0 处于 `RUNSTATE_offline` 状态，带有 `_VPF_down` 标志

**关键点**: Domain 0 在创建时就被暂停，防止在配置完成前运行。

#### 5.4.3 VCPU 0 的初始化

**分配 VCPU 0** (`create_dom0` 中):

```912:913:xen/xen/arch/x86/setup.c
    if ( alloc_dom0_vcpu0(d) == NULL )
        panic("Error creating d%uv0\n", domid);
```

**VCPU 初始化** (`construct_dom0` 中):

在 `construct_dom0()` 完成后，VCPU 0 的寄存器状态、页表等已设置好，但 VCPU 尚未上线。

**VCPU 上线**:

对于 Domain 0，VCPU 0 在构建过程中自动上线。对于普通 Domain，需要通过 `VCPUOP_up` hypercall 上线。

#### 5.4.4 系统状态切换

**位置**: `xen/xen/arch/x86/setup.c:730` 和 `xen/xen/arch/arm/setup.c:960`

**x86 架构**:

```730:741:xen/xen/arch/x86/setup.c
static void noreturn init_done(void)
{
    // ... 其他初始化 ...

    if ( (err = xsm_set_system_active()) != 0 )
        panic("xsm: unable to switch to SYSTEM_ACTIVE privilege: %d\n", err);

    system_state = SYS_STATE_active;

    domain_unpause_by_systemcontroller(dom0);
```

**ARM 架构**:

```957:963:xen/xen/arch/arm/setup.c
    if ( (rc = xsm_set_system_active()) != 0 )
        panic("xsm: unable to switch to SYSTEM_ACTIVE privilege: %d\n", rc);

    system_state = SYS_STATE_active;

    for_each_domain( d )
        domain_unpause_by_systemcontroller(d);
```

**关键步骤**:
1. **XSM 激活**: 将 Xen 安全模块切换到 `SYSTEM_ACTIVE` 状态
2. **系统状态**: 设置为 `SYS_STATE_active`，表示系统完全激活
3. **取消暂停**: 调用 `domain_unpause_by_systemcontroller()` 取消暂停 Domain 0

#### 5.4.5 domain_unpause_by_systemcontroller 流程

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

**流程步骤**:

1. **减少控制器暂停计数**: 使用原子操作将 `controller_pause_count` 从 1 减到 0
2. **标记创建完成**: 如果计数首次降到 0，标记 Domain 创建完成
   - 设置 `creation_finished = true`
   - 调用 `arch_domain_creation_finished()` 执行架构特定的完成操作
3. **调用 domain_unpause**: 调用 `domain_unpause()` 取消暂停 Domain

#### 5.4.6 domain_unpause 流程

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

**流程步骤**:

1. **架构特定恢复**: 调用 `arch_domain_unpause()` 执行架构特定的恢复操作
2. **减少暂停计数**: 原子减少 `pause_count`
3. **唤醒 VCPU**: 如果暂停计数降到 0，遍历所有 VCPU 并调用 `vcpu_wake()`

#### 5.4.7 vcpu_wake 流程

**位置**: `xen/xen/common/sched/core.c:965`

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
    // ... 其他情况处理 ...

    unit_schedule_unlock_irqrestore(lock, flags, unit);

    rcu_read_unlock(&sched_res_rculock);
}
```

**流程步骤**:

1. **获取调度锁**: 获取调度单元的锁
2. **检查可运行性**: 检查 VCPU 是否可运行（`vcpu_runnable()`）
3. **状态转换**: 如果状态是 `RUNSTATE_blocked` 或更高，转换为 `RUNSTATE_runnable`
4. **通知调度器**: 调用 `sched_wake()` 通知调度器该 VCPU 可运行
5. **强制上下文切换**: 如果调度单元正在运行但 VCPU 未运行，设置强制上下文切换标志并触发软中断

#### 5.4.8 调度器调度

**调度器选择**:

调度器会从可运行的 VCPU 中选择一个进行调度。对于 Domain 0，VCPU 0 现在处于 `RUNSTATE_runnable` 状态，可以被调度。

**调度决策**:

- 调度器根据调度策略（Credit、Credit2、RTDS 等）选择下一个运行的 VCPU
- Domain 0 的 VCPU 通常有较高的优先级
- 如果 Domain 0 的 VCPU 是唯一可运行的，它会被立即调度

#### 5.4.9 上下文切换

**位置**: `xen/xen/arch/x86/domain.c:2037` (x86) 或 `xen/xen/arch/arm/domain.c:319` (ARM)

**关键特性**: `context_switch` 可以在**不同 domain 之间**进行切换。这是 Xen 多域虚拟化的核心机制，允许单个物理 CPU 在多个 guest domain 之间共享。

**x86 架构上下文切换**:

```2037:2126:xen/xen/arch/x86/domain.c
void context_switch(struct vcpu *prev, struct vcpu *next)
{
    unsigned int cpu = smp_processor_id();
    struct cpu_info *info = get_cpu_info();
    const struct domain *prevd = prev->domain, *nextd = next->domain;
    unsigned int dirty_cpu = read_atomic(&next->dirty_cpu);

    // ... 检查和处理 ...

    _update_runstate_area(prev);
    vpmu_switch_from(prev);
    np2m_schedule(NP2M_SCHEDLE_OUT);

    // ... 保存前一个 VCPU 的状态 ...

    local_irq_disable();

    set_current(next);

    if ( (per_cpu(curr_vcpu, cpu) == next) ||
         (is_idle_domain(nextd) && cpu_online(cpu)) )
    {
        local_irq_enable();
    }
    else
    {
        __context_switch();

        // ... 恢复下一个 VCPU 的状态 ...

        if ( is_pv_domain(nextd) )
            load_segments(next);

        // ... 其他设置 ...
    }

    sched_context_switched(prev, next);

    _update_runstate_area(next);
    vpmu_switch_to(next);
    np2m_schedule(NP2M_SCHEDLE_IN);
}
```

**跨 Domain 切换的关键代码** (`__context_switch`):

`__context_switch` 是实际执行上下文切换的核心函数，负责保存前一个 VCPU 的状态并恢复下一个 VCPU 的状态。

#### x86 架构的 __context_switch

```1967:2035:xen/xen/arch/x86/domain.c
static void __context_switch(void)
{
    struct cpu_user_regs *stack_regs = guest_cpu_user_regs();
    unsigned int          cpu = smp_processor_id();
    struct vcpu          *p = per_cpu(curr_vcpu, cpu);  // 前一个 VCPU
    struct vcpu          *n = current;                  // 下一个 VCPU
    struct domain        *pd = p->domain, *nd = n->domain;

    ASSERT(p != n);
    ASSERT(!vcpu_cpu_dirty(n));

    // ========== 第一阶段：保存前一个 VCPU 的状态 ==========
    if ( !is_idle_domain(pd) )
    {
        ASSERT(read_atomic(&p->dirty_cpu) == cpu);
        // 1. 保存通用寄存器到 VCPU 结构
        memcpy(&p->arch.user_regs, stack_regs, CTXT_SWITCH_STACK_BYTES);
        // 2. 保存 FPU/浮点状态
        vcpu_save_fpu(p);
        // 3. 调用架构特定的保存函数（PV 或 HVM）
        pd->arch.ctxt_switch->from(p);
    }

    // ========== 第二阶段：准备切换到下一个 VCPU ==========
    /*
     * 标记 CPU 在下一个 domain 的 dirty cpumask 中
     * 这避免 EPT 刷新等操作的竞争条件
     */
    if ( pd != nd )  // 跨 domain 切换
    {
        cpumask_set_cpu(cpu, nd->dirty_cpumask);
    }
    write_atomic(&n->dirty_cpu, cpu);

    // ========== 第三阶段：恢复下一个 VCPU 的状态 ==========
    if ( !is_idle_domain(nd) )
    {
        // 1. 恢复通用寄存器从 VCPU 结构
        memcpy(stack_regs, &n->arch.user_regs, CTXT_SWITCH_STACK_BYTES);
        // 2. 恢复 XSAVE 相关状态（如果支持）
        if ( cpu_has_xsave )
        {
            if ( !set_xcr0(n->arch.xcr0 ?: XSTATE_FP_SSE) )
                BUG();
            if ( cpu_has_xsaves && is_hvm_vcpu(n) )
                set_msr_xss(n->arch.msrs->xss.raw);
        }
        // 3. 恢复 FPU/浮点状态
        vcpu_restore_fpu_nonlazy(n, false);
        // 4. 调用架构特定的恢复函数（PV 或 HVM）
        nd->arch.ctxt_switch->to(n);
    }

    // ========== 第四阶段：切换系统资源 ==========
    // 1. PSR (Platform Shared Resource) 切换
    psr_ctxt_switch_to(nd);

    // 2. GDT (Global Descriptor Table) 管理
    if ( need_full_gdt(nd) )
        update_xen_slot_in_full_gdt(n, cpu);
    if ( per_cpu(full_gdt_loaded, cpu) &&
         ((p->vcpu_id != n->vcpu_id) || !need_full_gdt(nd)) )
        load_default_gdt(cpu);

    // 3. **关键：切换页表基址（CR3）**
    write_ptbase(n);  // 这是内存隔离的核心！

#if defined(CONFIG_PV) && defined(CONFIG_HVM)
    // 4. SVM 特定优化：预取 VMCB
    if ( cpu_has_svm && is_pv_64bit_domain(nd) && !is_idle_domain(nd) )
        svm_load_segs_prefetch();
#endif

    // 5. 如果需要，加载完整 GDT
    if ( need_full_gdt(nd) && !per_cpu(full_gdt_loaded, cpu) )
        load_full_gdt(n, cpu);

    // ========== 第五阶段：清理和更新 ==========
    if ( pd != nd )  // 跨 domain 切换时清理
        cpumask_clear_cpu(cpu, pd->dirty_cpumask);
    write_atomic(&p->dirty_cpu, VCPU_CPU_CLEAN);

    // 更新当前运行的 VCPU
    per_cpu(curr_vcpu, cpu) = n;
}
```

#### ARM 架构的 __context_switch

ARM 架构的 `__context_switch` 在汇编中实现，更简洁：

```629:649:xen/xen/arch/arm/arm64/entry.S
ENTRY(__context_switch)
        add     x8, x0, #VCPU_arch_saved_context
        mov     x9, sp
        stp     x19, x20, [x8], #16         /* 保存被调用者保存寄存器 */
        stp     x21, x22, [x8], #16
        stp     x23, x24, [x8], #16
        stp     x25, x26, [x8], #16
        stp     x27, x28, [x8], #16
        stp     x29, x9, [x8], #16         /* 保存帧指针和栈指针 */
        str     lr, [x8]                    /* 保存返回地址 */

        add     x8, x1, #VCPU_arch_saved_context
        ldp     x19, x20, [x8], #16         /* 恢复被调用者保存寄存器 */
        ldp     x21, x22, [x8], #16
        ldp     x23, x24, [x8], #16
        ldp     x25, x26, [x8], #16
        ldp     x27, x28, [x8], #16
        ldp     x29, x9, [x8], #16         /* 恢复帧指针和栈指针 */
        ldr     lr, [x8]                    /* 恢复返回地址 */
        mov     sp, x9                      /* 恢复栈指针 */
        ret                                 /* 返回 */
```

ARM 架构中，`__context_switch` 只保存/恢复被调用者保存的寄存器（callee-saved registers），其他状态（如系统寄存器、页表等）由 `ctxt_switch_from` 和 `ctxt_switch_to` 处理。

#### __context_switch 的详细操作步骤

**x86 架构的完整流程**:

1. **保存前一个 VCPU 状态**:
   - **通用寄存器**: `memcpy(&p->arch.user_regs, stack_regs, ...)` - 保存所有通用寄存器
   - **FPU 状态**: `vcpu_save_fpu(p)` - 保存浮点单元状态
   - **架构特定状态**: `pd->arch.ctxt_switch->from(p)` - 调用 PV 或 HVM 特定的保存函数
     - **PV**: `paravirt_ctxt_switch_from()` - 保存段寄存器、控制寄存器等
     - **HVM (VMX)**: `vmx_ctxt_switch_from()` - 保存 VMCS、MSR、DR 寄存器等
     - **HVM (SVM)**: `svm_ctxt_switch_from()` - 保存 VMCB 相关状态

2. **更新 Dirty CPU 跟踪**:
   - 如果跨 domain 切换，更新 `dirty_cpumask`，用于跟踪哪些 CPU 正在访问某个 domain
   - 设置 `n->dirty_cpu = cpu`，标记下一个 VCPU 正在此 CPU 上运行

3. **恢复下一个 VCPU 状态**:
   - **通用寄存器**: `memcpy(stack_regs, &n->arch.user_regs, ...)` - 恢复所有通用寄存器
   - **XSAVE 状态**: 如果 CPU 支持 XSAVE，恢复扩展状态（AVX、MPX 等）
   - **FPU 状态**: `vcpu_restore_fpu_nonlazy()` - 恢复浮点单元状态
   - **架构特定状态**: `nd->arch.ctxt_switch->to(n)` - 调用 PV 或 HVM 特定的恢复函数

4. **切换系统资源**:
   - **PSR**: `psr_ctxt_switch_to()` - 切换平台共享资源（Cache Allocation Technology 等）
   - **GDT**: 根据需要加载或更新全局描述符表
   - **页表**: `write_ptbase(n)` - **最关键的操作**，切换页表基址（CR3）
     - 对于 PV: 切换到 guest 的页表
     - 对于 HVM: 切换到 EPT/NPT 的根页表
   - **SVM 优化**: 如果使用 SVM，预取 VMCB

5. **清理和更新**:
   - 清除前一个 domain 的 `dirty_cpumask`（如果跨 domain）
   - 标记前一个 VCPU 的 `dirty_cpu` 为 `VCPU_CPU_CLEAN`
   - 更新 `per_cpu(curr_vcpu, cpu) = n`，标记当前运行的 VCPU

**ARM 架构的完整流程**:

ARM 架构的 `__context_switch` 在汇编中实现，只处理寄存器保存/恢复。其他操作在 C 代码中完成：

1. **寄存器保存/恢复** (汇编):
   - 保存 x19-x30（被调用者保存寄存器）
   - 保存栈指针 (sp) 和链接寄存器 (lr)
   - 恢复下一个 VCPU 的寄存器

2. **系统寄存器切换** (C 代码，在 `ctxt_switch_from`/`ctxt_switch_to`):
   - **P2M**: 保存/恢复物理到机器的映射状态
   - **VGIC**: 保存/恢复虚拟通用中断控制器状态
   - **MMU**: 保存/恢复页表基址（TTBR0/TTBR1）、页表控制寄存器（TCR）等
   - **控制寄存器**: CPACR、CONTEXTIDR、TPIDR 等
   - **定时器**: 虚拟定时器状态
   - **浮点**: VFP 状态

#### 关键函数说明

**write_ptbase (x86)**:

```518:543:xen/xen/arch/x86/mm.c
void write_ptbase(struct vcpu *v)
{
    struct cpu_info *cpu_info = get_cpu_info();
    unsigned long new_cr4;

    new_cr4 = (is_pv_vcpu(v) && !is_idle_vcpu(v))
              ? pv_make_cr4(v) : mmu_cr4_features;

    if ( is_pv_vcpu(v) && v->domain->arch.pv.xpti )
    {
        // XPTI (Xen Page Table Isolation) 模式
        cpu_info->root_pgt_changed = true;
        cpu_info->pv_cr3 = __pa(this_cpu(root_pgt));
        if ( new_cr4 & X86_CR4_PCIDE )
            cpu_info->pv_cr3 |= get_pcid_bits(v, true);
        switch_cr3_cr4(v->arch.cr3, new_cr4);
    }
    else
    {
        // 普通模式：直接切换 CR3
        cpu_info->use_pv_cr3 = false;
        cpu_info->xen_cr3 = 0;
        switch_cr3_cr4(v->arch.cr3, new_cr4);
        cpu_info->pv_cr3 = 0;
    }
}
```

**ctxt_switch_from (ARM)**:

```86:166:xen/xen/arch/arm/domain.c
static void ctxt_switch_from(struct vcpu *p)
{
    if ( is_idle_vcpu(p) )
        return;

    // 1. P2M 状态保存
    p2m_save_state(p);

    // 2. CP15 寄存器
    p->arch.csselr = READ_SYSREG(CSSELR_EL1);

    // 3. VFP 状态
    vfp_save_state(p);

    // 4. 控制寄存器
    p->arch.cpacr = READ_SYSREG(CPACR_EL1);
    p->arch.contextidr = READ_SYSREG(CONTEXTIDR_EL1);
    p->arch.tpidr_el0 = READ_SYSREG(TPIDR_EL0);
    p->arch.tpidrro_el0 = READ_SYSREG(TPIDRRO_EL0);
    p->arch.tpidr_el1 = READ_SYSREG(TPIDR_EL1);

    // 5. 定时器
    p->arch.cntkctl = READ_SYSREG(CNTKCTL_EL1);
    virt_timer_save(p);

    // 6. MMU 相关寄存器
    p->arch.vbar = READ_SYSREG(VBAR_EL1);
    p->arch.ttbcr = READ_SYSREG(TCR_EL1);
    p->arch.ttbr0 = READ_SYSREG64(TTBR0_EL1);
    p->arch.ttbr1 = READ_SYSREG64(TTBR1_EL1);
    // ... MAIR、AMAIR、DACR 等 ...

    // 7. 故障状态寄存器
    p->arch.far = READ_SYSREG64(FAR_EL1);
    p->arch.esr = READ_SYSREG64(ESR_EL1);
    // ... AFSR 等 ...

    // 8. VGIC 状态
    gic_save_state(p);
}
```

**ctxt_switch_to (ARM)**:

```168:232:xen/xen/arch/arm/domain.c
static void ctxt_switch_to(struct vcpu *n)
{
    if ( is_idle_vcpu(n) )
        return;

    // 1. 虚拟化 ID 寄存器
    vpidr = READ_SYSREG(MIDR_EL1);
    WRITE_SYSREG(vpidr, VPIDR_EL2);
    WRITE_SYSREG(n->arch.vmpidr, VMPIDR_EL2);

    // 2. VGIC 状态恢复
    gic_restore_state(n);

    // 3. 故障状态寄存器恢复
    WRITE_SYSREG64(n->arch.far, FAR_EL1);
    WRITE_SYSREG64(n->arch.esr, ESR_EL1);
    // ...

    // 4. MMU 相关寄存器恢复
    WRITE_SYSREG(n->arch.vbar, VBAR_EL1);
    WRITE_SYSREG(n->arch.ttbcr, TCR_EL1);
    WRITE_SYSREG64(n->arch.ttbr0, TTBR0_EL1);
    WRITE_SYSREG64(n->arch.ttbr1, TTBR1_EL1);
    // ... MAIR、AMAIR、DACR 等 ...

    // 5. P2M 状态恢复（必须在 MMU 之后）
    p2m_restore_state(n);

    // 6. 控制寄存器恢复
    WRITE_SYSREG(n->arch.cpacr, CPACR_EL1);
    // ...
}
```

#### 总结

`__context_switch` 是 Xen 虚拟化的核心函数，它执行以下关键操作：

1. **状态保存**: 保存前一个 VCPU 的所有状态（寄存器、FPU、架构特定状态）
2. **状态恢复**: 恢复下一个 VCPU 的所有状态
3. **页表切换**: 切换页表基址，实现内存隔离（x86: CR3, ARM: TTBR）
4. **资源切换**: 切换系统资源（GDT、PSR 等）
5. **跟踪更新**: 更新 CPU 跟踪信息（dirty_cpu、dirty_cpumask）

这些操作确保了不同 domain 之间的完全隔离，同时允许单个物理 CPU 在多个 guest domain 之间高效切换。

**跨 Domain 切换的关键点**:

1. **Domain 检查**: 代码中明确检查 `pd != nd`，表示在不同 domain 之间切换
2. **状态保存**: 调用 `pd->arch.ctxt_switch->from(p)` 保存前一个 domain 的状态
3. **状态恢复**: 调用 `nd->arch.ctxt_switch->to(n)` 恢复下一个 domain 的状态
4. **页表切换**: `write_ptbase(n)` 切换页表基址（CR3/x86 或 TTBR/ARM），这是跨 domain 隔离的关键
5. **Dirty CPU 管理**: 跨 domain 切换时需要更新 `dirty_cpumask`，用于跟踪哪些 CPU 正在访问某个 domain

**跨 Domain 切换示例**:

```
场景：CPU 0 从 Domain 0 切换到 Domain U

1. 当前状态：
   - CPU 0 正在运行 Domain 0 的 VCPU 0
   - per_cpu(curr_vcpu, 0) = dom0_vcpu0
   - CR3 = Domain 0 的页表基址

2. 调度器决策：
   - 调度器选择 Domain U 的 VCPU 0 作为下一个运行的 VCPU
   - 调用 context_switch(dom0_vcpu0, domu_vcpu0)

3. 上下文切换过程：
   a. 保存 Domain 0 状态：
      - 保存寄存器到 dom0_vcpu0->arch.user_regs
      - 保存 FPU 状态
      - 调用 Domain 0 的 ctxt_switch->from()

   b. 检查跨 Domain：
      - prevd = Domain 0
      - nextd = Domain U
      - pd != nd，执行跨 domain 切换逻辑

   c. 更新 dirty cpumask：
      - cpumask_set_cpu(0, domu->dirty_cpumask)
      - cpumask_clear_cpu(0, dom0->dirty_cpumask)

   d. 恢复 Domain U 状态：
      - 恢复寄存器从 domu_vcpu0->arch.user_regs
      - 恢复 FPU 状态
      - 调用 Domain U 的 ctxt_switch->to()

   e. 切换页表：
      - write_ptbase(domu_vcpu0)
      - CR3 = Domain U 的页表基址

   f. 更新当前 VCPU：
      - per_cpu(curr_vcpu, 0) = domu_vcpu0

4. 结果：
   - CPU 0 现在运行 Domain U 的代码
   - Domain 0 的状态已保存，可以稍后恢复
   - 内存隔离通过页表切换实现
```

**切换类型**:

`context_switch` 可以处理以下类型的切换：

1. **同 Domain 内切换**: `prev->domain == next->domain`
   - 例如：Domain 0 的 VCPU 0 → Domain 0 的 VCPU 1
   - 不需要切换页表，但需要切换寄存器状态

2. **跨 Domain 切换**: `prev->domain != next->domain`
   - 例如：Domain 0 的 VCPU 0 → Domain U 的 VCPU 0
   - 需要切换页表、寄存器状态、FPU 状态等

3. **切换到 Idle Domain**: `next->domain == idle_domain`
   - CPU 空闲时运行 idle VCPU
   - 不需要恢复 guest 状态

4. **从 Idle Domain 切换**: `prev->domain == idle_domain`
   - 从空闲状态切换到 guest domain
   - 只需要恢复 guest 状态，不需要保存 idle 状态

**ARM 架构上下文切换**:

```319:334:xen/xen/arch/arm/domain.c
void context_switch(struct vcpu *prev, struct vcpu *next)
{
    ASSERT(local_irq_is_enabled());
    ASSERT(prev != next);
    ASSERT(!vcpu_cpu_dirty(next));

    update_runstate_area(prev);

    local_irq_disable();

    set_current(next);

    prev = __context_switch(prev, next);

    schedule_tail(prev);
}
```

**上下文切换步骤**:

1. **保存前一个 VCPU 状态**:
   - 更新运行状态区域
   - 保存寄存器状态
   - 保存 FPU/浮点状态
   - 保存架构特定状态

2. **切换到下一个 VCPU**:
   - 设置当前 VCPU (`set_current(next)`)
   - 恢复寄存器状态
   - 恢复页表（CR3/x86 或 TTBR/ARM）
   - 恢复 FPU/浮点状态
   - 恢复架构特定状态

3. **更新运行状态**:
   - 前一个 VCPU: `RUNSTATE_running` → `RUNSTATE_runnable` 或 `RUNSTATE_blocked`
   - 下一个 VCPU: `RUNSTATE_runnable` → `RUNSTATE_running`

#### 5.4.10 进入 Domain 0 执行

**x86 架构**:

上下文切换后，CPU 会：
1. 从 Domain 0 的入口点开始执行（由 `construct_dom0` 设置）
2. 对于 PV Domain，从 `start_info` 结构指定的入口点开始
3. 对于 PVH Domain，从 ACPI 表或启动信息指定的入口点开始

**ARM 架构**:

上下文切换后，CPU 会：
1. 从 `continue_new_vcpu()` 继续执行
2. 调用 `return_to_new_vcpu32()` 或 `return_to_new_vcpu64()`
3. 最终进入 Domain 0 的内核入口点

#### 5.4.11 完整流程总结

```
1. Domain 0 创建
   ├─> domain_create()
   ├─> controller_pause_count = 1
   ├─> pause_count = 1
   └─> VCPU 0: RUNSTATE_offline, _VPF_down

2. Domain 0 构建
   ├─> construct_dom0()
   ├─> 内存分配和页表设置
   ├─> 内核加载
   └─> VCPU 0 初始化（寄存器、页表等）

3. VCPU 0 上线
   ├─> VCPUOP_up (隐式或显式)
   ├─> 清除 _VPF_down 标志
   └─> VCPU 0: RUNSTATE_runnable

4. 系统激活
   ├─> xsm_set_system_active()
   ├─> system_state = SYS_STATE_active
   └─> domain_unpause_by_systemcontroller(dom0)
       ├─> controller_pause_count: 1 → 0
       ├─> creation_finished = true
       └─> domain_unpause()
           ├─> pause_count: 1 → 0
           └─> vcpu_wake(v) for each vCPU
               ├─> runstate: → RUNSTATE_runnable
               └─> sched_wake()

5. 调度器调度
   ├─> 调度器选择 Domain 0 的 VCPU 0
   └─> context_switch(idle_vcpu, dom0_vcpu0)
       ├─> 保存 idle VCPU 状态
       ├─> 恢复 Domain 0 VCPU 0 状态
       └─> 设置当前 VCPU = dom0_vcpu0

6. Domain 0 开始运行
   └─> CPU 从 Domain 0 内核入口点开始执行
```

#### 5.4.12 关键机制说明

**暂停计数机制**:

- **`pause_count`**: Domain 级别的暂停计数，用于同步暂停操作
- **`controller_pause_count`**: 系统控制器级别的暂停计数，用于 Domain 创建流程
- 只有当两个计数都为 0 时，Domain 才能运行

**VCPU 状态机**:

```
RUNSTATE_offline → RUNSTATE_runnable → RUNSTATE_running
     ↑                    ↓                    ↓
     └────────────────────┴────────────────────┘
                    (阻塞或下线)
```

**调度器集成**:

- VCPU 唤醒后，调度器会将其加入运行队列
- 调度器根据策略选择下一个运行的 VCPU
- 上下文切换由调度器触发，在中断返回或调度点执行

## 六、启动流程图

### 6.1 x86 架构启动流程

```
Bootloader (GRUB/EFI)
    |
    v
[Multiboot Protocol]
    |
    v
[__start_xen()]
    |
    v
[关键区域初始化]
    ├─> init_shadow_spec_ctrl_state()
    ├─> percpu_init_areas()
    ├─> init_idt_traps()
    └─> load_system_tables()
    |
    v
[启动协议处理]
    ├─> pvh_init() 或 Multiboot 解析
    └─> cmdline_parse()
    |
    v
[控制台初始化]
    ├─> hypervisor_probe()
    ├─> ns16550_init()
    └─> console_init_preirq()
    |
    v
[内存映射初始化]
    ├─> early_cpu_init()
    ├─> early_microcode_init()
    └─> E820 内存映射解析
    |
    v
[内存管理初始化]
    ├─> init_frametable()
    ├─> acpi_numa_init()
    ├─> end_boot_allocator()
    └─> system_state = SYS_STATE_boot
    |
    v
[虚拟化初始化]
    ├─> vm_init()
    ├─> paging_init()
    └─> setup_system_domains()
    |
    v
[IOMMU 和中断]
    ├─> iommu_setup()
    └─> APIC 初始化
    |
    v
[Domain 0 创建]
    ├─> create_dom0()
    ├─> domain_create()
    └─> construct_dom0()
    |
    v
[系统激活]
    ├─> system_state = SYS_STATE_active
    └─> domain_unpause_by_systemcontroller()
```

### 6.2 ARM 架构启动流程

```
Bootloader (U-Boot/EFI)
    |
    v
[head.S: start]
    |
    v
[汇编初始化]
    ├─> CPU 初始化
    ├─> MMU 设置
    └─> 跳转到 C 代码
    |
    v
[start_xen()]
    |
    v
[早期初始化]
    ├─> percpu_init_areas()
    ├─> init_traps()
    ├─> setup_pagetables()
    └─> early_fdt_map()
    |
    v
[内存管理]
    ├─> setup_mm()
    ├─> end_boot_allocator()
    └─> system_state = SYS_STATE_boot
    |
    v
[虚拟化和设备树]
    ├─> vm_init()
    └─> dt_unflatten_host_device_tree()
    |
    v
[中断和平台]
    ├─> init_IRQ()
    ├─> gic_init()
    └─> smp_init_cpus()
    |
    v
[调度器和系统域]
    ├─> init_idle_domain()
    ├─> rcu_init()
    └─> setup_system_domains()
    |
    v
[多核启动]
    └─> cpu_up() for each CPU
    |
    v
[IOMMU 和分页]
    ├─> iommu_setup()
    └─> setup_virt_paging()
    |
    v
[Domain 0 创建]
    ├─> create_dom0()
    └─> construct_dom0()
    |
    v
[系统激活]
    ├─> system_state = SYS_STATE_active
    └─> domain_unpause_by_systemcontroller()
```

## 七、关键概念

### 7.1 启动分配器 (Boot Allocator)

**目的**: 在内存管理子系统完全初始化之前提供临时内存分配。

**特点**:
- 简单的内存分配机制
- 在 `end_boot_allocator()` 之后切换到正常分配器
- 启动阶段使用的内存会被回收

### 7.2 系统域 (System Domains)

Xen 维护几个特殊的系统域：

- **dom_xen**: Xen 自身的域（用于某些内部操作）
- **dom_io**: I/O 域（用于设备访问）
- **dom_cow**: Copy-on-Write 域（用于某些内存操作）

### 7.3 Domain 0 特权

Domain 0 通过 `CDF_privileged` 标志获得特权：

- **管理其他域**: 可以创建、销毁、管理其他 Domain
- **硬件访问**: 可以直接访问硬件设备
- **管理接口**: 提供 Xen 管理接口（通过 XenStore、XenBus 等）

### 7.4 启动模块

**x86 架构**:
- **模块 0**: Domain 0 内核
- **模块 1+**: Initrd、其他模块

**ARM 架构**:
- 通过 Device Tree 或 ACPI 描述
- 可以包含多个 Domain 的配置（dom0less 模式）

## 八、总结

Xen Hypervisor 的启动流程遵循以下模式：

1. **早期初始化**: CPU、异常处理、基本数据结构
2. **内存管理**: 内存映射、分配器初始化
3. **虚拟化**: 虚拟化子系统初始化
4. **设备**: 中断、IOMMU、平台设备初始化
5. **调度器**: 调度器、系统域初始化
6. **Domain 0**: 创建特权域
7. **系统激活**: 切换到激活状态，允许域运行

**关键要点**:
- 系统状态机管理启动过程
- 启动分配器在早期阶段提供内存
- Domain 0 是第一个也是最重要的域
- 系统激活后才允许域运行

## 九、Xen Hypervisor 与 TF-A 的对比

### 9.1 相似性

Xen Hypervisor 和 ARM 架构下的 **TF-A (Trusted Firmware-A)** 确实有相似之处：

1. **都是"世界切换"工具**:
   - **TF-A**: 在安全世界（Secure World, EL1）和非安全世界（Non-secure World, EL1）之间切换
   - **Xen**: 在不同 Domain 之间切换

2. **都运行在最高特权级**:
   - **TF-A**: 运行在 EL3（最高特权级）
   - **Xen**: 运行在 Ring 0 (x86) 或 EL2 (ARM)

3. **都提供隔离机制**:
   - **TF-A**: 通过 TrustZone 硬件提供安全/非安全隔离
   - **Xen**: 通过虚拟化硬件提供 Domain 隔离

4. **都管理执行上下文**:
   - **TF-A**: 保存/恢复安全世界和非安全世界的寄存器状态
   - **Xen**: 保存/恢复不同 Domain 的 VCPU 状态

### 9.2 关键区别：调度能力

**重要区别**: Xen Hypervisor **有完整的调度器**，而 TF-A **没有调度器**。

#### TF-A 的特点

**TF-A 是被动的切换工具**:

- **触发方式**: 响应 SMC (Secure Monitor Call) 指令调用
- **切换决策**: 由调用者（安全世界或非安全世界）决定何时切换
- **无调度器**: TF-A 本身不决定何时切换、切换到哪个世界
- **功能**: 仅提供切换机制，不管理时间片、优先级等

**TF-A 的工作流程**:
```
安全世界 → SMC 调用 → TF-A (EL3) → 切换到非安全世界
非安全世界 → SMC 调用 → TF-A (EL3) → 切换到安全世界
```

#### Xen Hypervisor 的特点

**Xen 是主动的调度器**:

- **触发方式**:
  - 定时器中断（时间片到期）
  - VCPU 阻塞（等待 I/O、事件等）
  - VCPU 唤醒（事件到达）
  - 主动调用 `schedule()`
- **切换决策**: **调度器主动决定**何时切换、切换到哪个 VCPU
- **完整调度器**: 有多种调度算法（Credit、Credit2、RTDS、ARINC653 等）
- **功能**: 管理运行队列、优先级、时间片、负载均衡等

**Xen 的工作流程**:
```
当前 VCPU 运行 → 调度器决策（基于优先级、时间片等）→ 选择下一个 VCPU → 上下文切换
```

#### Xen 调度器的核心组件

**1. 调度器主函数** (`schedule()`):

```2654:2716:xen/xen/common/sched/core.c
static void cf_check schedule(void)
{
    struct vcpu          *vnext, *vprev = current;
    struct sched_unit    *prev = vprev->sched_unit, *next = NULL;
    s_time_t              now;
    // ...

    // 调用调度器策略函数，选择下一个运行的 VCPU
    next = do_schedule(prev, now, cpu);

    // 执行上下文切换
    vnext = sched_unit2vcpu_cpu(next, cpu);
    sched_context_switch(vprev, vnext, ...);
}
```

**2. 调度策略函数** (`do_schedule()`):

```2277:2295:xen/xen/common/sched/core.c
static struct sched_unit *do_schedule(struct sched_unit *prev, s_time_t now,
                                      unsigned int cpu)
{
    struct sched_resource *sr = get_sched_res(cpu);
    struct scheduler *sched = sr->scheduler;
    struct sched_unit *next;

    /* 调用策略特定的调度决策函数 */
    sched->do_schedule(sched, prev, now, sched_tasklet_check(cpu));

    /* 获取调度器选择的下一个任务 */
    next = prev->next_task;

    // 设置时间片定时器
    if ( prev->next_time >= 0 )
        set_timer(&sr->s_timer, now + prev->next_time);

    return next;
}
```

**3. Credit 调度器示例** (`csched_schedule()`):

```1846:2020:xen/xen/common/sched/credit.c
static void cf_check csched_schedule(
    const struct scheduler *ops, struct sched_unit *unit, s_time_t now,
    bool tasklet_work_scheduled)
{
    // ...

    /* 更新当前 VCPU 的信用值 */
    if ( !is_idle_unit(unit) )
    {
        burn_credits(scurr, now);  // 消耗信用值
        scurr->start_time -= now;
        scurr->last_sched_time = now;
    }

    /* 从运行队列中选择下一个 VCPU */
    // - 检查是否有 tasklet 工作
    // - 检查速率限制
    // - 选择优先级最高的 VCPU
    // - 负载均衡

    snext = __runq_elem(runq->next);  // 从运行队列选择

    // ...

    /* 返回选择的下一个任务 */
    unit->next_task = snext->unit;
}
```

### 9.3 对比总结

| 特性 | TF-A | Xen Hypervisor |
|------|------|----------------|
| **角色** | 被动切换工具 | 主动调度器 |
| **触发方式** | SMC 调用 | 定时器、事件、主动调用 |
| **切换决策** | 由调用者决定 | 由调度器决定 |
| **调度算法** | 无 | Credit、Credit2、RTDS、ARINC653 等 |
| **运行队列** | 无 | 有（按优先级排序） |
| **时间片管理** | 无 | 有（时间片、信用值等） |
| **负载均衡** | 无 | 有（SMP 负载均衡） |
| **优先级管理** | 无 | 有（动态优先级） |
| **VCPU 状态管理** | 无 | 有（runnable、blocked、running 等） |

### 9.4 本质区别

**TF-A**:
- **本质**: 一个**被动的切换机制**，响应调用者的请求进行世界切换
- **类比**: 像是一个"开关"，调用者控制何时切换
- **不负责**: 不决定何时切换、不管理时间分配

**Xen Hypervisor**:
- **本质**: 一个**主动的调度系统**，管理 CPU 时间分配和 VCPU 执行顺序
- **类比**: 像是一个"交通指挥系统"，主动管理所有"车辆"（VCPU）的运行
- **负责**: 决定何时切换、切换到哪个 VCPU、分配多少时间片

### 9.5 结论

虽然 Xen Hypervisor 和 TF-A 都提供"世界切换"功能，但：

1. **TF-A**: 只是切换工具，**没有调度能力**
2. **Xen**: 既是切换工具，**也是完整的调度器**

Xen 的调度器是虚拟化系统的核心组件，它：
- 主动管理 CPU 时间分配
- 决定 VCPU 的执行顺序
- 提供公平性、实时性等调度策略
- 支持多核负载均衡

这使得 Xen 能够在一个物理 CPU 上高效地运行多个 guest domain，而不仅仅是简单地切换执行上下文。

### 9.6 深入思考：为什么 TF-A 不集成调度能力？为什么 Xen 不将调度能力下放到 Domain 0？

这两个问题触及了系统架构设计的核心哲学。

#### 9.6.1 为什么 TF-A 不集成调度能力？

**1. 设计目标不同**

**TF-A 的设计目标**:
- **安全固件**: TF-A 是 TrustZone 的安全固件，目标是提供安全隔离
- **最小可信计算基 (TCB)**: 遵循"最小化可信代码"的安全原则
- **单一职责**: 只负责安全世界和非安全世界的切换

**如果 TF-A 集成调度器**:
- 会大幅增加代码复杂度
- 违背"最小可信计算基"原则
- 增加安全攻击面
- 偏离了安全固件的设计目标

**2. TrustZone 的设计哲学**

TrustZone 的设计哲学是：
- **硬件隔离**: 通过硬件机制（NS bit）提供安全/非安全隔离
- **被动切换**: 响应 SMC 调用进行切换
- **不管理资源**: 不负责 CPU 时间分配、优先级管理等

如果 TF-A 变成调度器，就变成了虚拟化平台，而不是安全固件。

**3. 安全性和复杂性的矛盾**

```
安全性 ∝ 1 / 代码复杂度
```

- **简单 = 安全**: 代码越简单，安全漏洞越少
- **复杂 = 风险**: 调度器是复杂系统，容易引入安全漏洞
- **TCB 原则**: 可信计算基应该尽可能小

**4. 实际考虑**

如果 TF-A 集成调度器：
- 需要管理多个安全世界实例（目前只有一个）
- 需要时间片管理、优先级管理等复杂机制
- 需要处理调度公平性、实时性等问题
- 实际上就变成了一个 Type-1 Hypervisor

但这样会：
- 失去安全固件的简单性
- 增加安全风险
- 偏离 TrustZone 的设计目标

#### 9.6.2 为什么 Xen 不将调度能力下放到 Domain 0？

**1. Domain 0 是 Guest，不是 Hypervisor**

**关键事实**: Domain 0 就是一个普通的虚拟机，只是被赋予了特权。

```11:11:xen/xen/docs/admin-guide/introduction.rst
**Domain 0 就是一个普通的虚拟机，只是它被赋予了特权，可以调用 Xen 的管理接口。**
```

如果调度器在 Domain 0：
- Domain 0 崩溃 → 调度器崩溃 → **整个系统崩溃**
- Domain 0 被攻击 → 攻击者控制调度器 → **可以控制所有 Domain**

**2. 隔离和公平性要求**

**调度器必须在 Hypervisor 层**:

```
┌─────────────────────────────────────┐
│     Hypervisor (调度器在这里)      │  ← 必须在这里
├─────────────────────────────────────┤
│  Domain 0  │  Domain 1  │  Domain 2 │  ← Guest 层
└─────────────────────────────────────┘
```

**原因**:
- **隔离**: 调度器必须独立于所有 Domain，不能被任何 Domain 控制
- **公平性**: 调度器必须公平分配 CPU 时间，Domain 0 不应该有特殊优势
- **可靠性**: 调度器崩溃会导致整个系统崩溃，必须在最稳定的层

**3. Type-1 vs Type-2 Hypervisor 的核心区别**

**Type-1 Hypervisor (Xen)**:
- 调度器在 Hypervisor 层
- Hypervisor 直接管理硬件和资源
- Domain 0 只是第一个特权 Guest

**Type-2 Hypervisor (KVM/QEMU)**:
- 调度器在 Host OS（如 Linux）
- Host OS 崩溃 → 所有 Guest 崩溃
- 这是 Type-2 的固有缺陷

**如果 Xen 将调度器下放到 Domain 0**:
- 就变成了 Type-2 Hypervisor
- 失去了 Type-1 的优势（可靠性、隔离性）
- Domain 0 崩溃会导致整个系统崩溃

**4. 代码证据**

**Domain 0 崩溃的处理**:

```33:48:xen/xen/common/shutdown.c
void hwdom_shutdown(u8 reason)
{
    switch ( reason )
    {
    case SHUTDOWN_poweroff:
        printk("Hardware Dom%u halted: halting machine\n",
               hardware_domain->domain_id);
        machine_halt();
        break;

    case SHUTDOWN_crash:
        debugger_trap_immediate();
        printk("Hardware Dom%u crashed: ", hardware_domain->domain_id);
        kexec_crash(CRASHREASON_HWDOM);
        maybe_reboot();
        break;
    }
}
```

**关键点**: Domain 0（硬件域）崩溃会导致**整个机器重启**！

如果调度器在 Domain 0：
- Domain 0 崩溃 → 调度器崩溃 → 无法调度其他 Domain → 系统崩溃

**5. 安全考虑**

**如果调度器在 Domain 0**:

```
攻击场景：
1. 攻击者攻破 Domain 0
2. 控制调度器
3. 可以：
   - 停止其他 Domain 的运行
   - 给恶意 Domain 分配更多 CPU 时间
   - 拒绝调度某些 Domain
   - 完全控制系统资源分配
```

**调度器在 Hypervisor 层**:
- 即使 Domain 0 被攻破，调度器仍然安全
- 攻击者无法控制调度决策
- 其他 Domain 仍然可以正常运行

**6. 实际架构对比**

**当前架构（调度器在 Hypervisor）**:

```
┌─────────────────────────────────────────┐
│  Hypervisor (调度器)                    │  ← 独立、安全、可靠
├─────────────────────────────────────────┤
│  Domain 0  │  Domain 1  │  Domain 2    │  ← Guest，可以崩溃
└─────────────────────────────────────────┘

优势：
✅ Domain 0 崩溃不影响调度器
✅ 调度器独立于所有 Domain
✅ 保证公平性和隔离性
```

**假设架构（调度器在 Domain 0）**:

```
┌─────────────────────────────────────────┐
│  Hypervisor (仅提供切换机制)            │
├─────────────────────────────────────────┤
│  Domain 0 (调度器) │  Domain 1 │  Dom2 │  ← 调度器在 Guest
└─────────────────────────────────────────┘

问题：
❌ Domain 0 崩溃 → 调度器崩溃 → 系统崩溃
❌ Domain 0 被攻击 → 调度器被控制
❌ Domain 0 可以给自己分配更多资源
❌ 失去了 Type-1 Hypervisor 的优势
```

#### 9.6.3 总结

**TF-A 不集成调度能力的原因**:
1. ✅ **设计目标**: 安全固件，不是虚拟化平台
2. ✅ **TCB 原则**: 最小化可信计算基
3. ✅ **安全性**: 简单性 = 安全性
4. ✅ **职责分离**: 只负责切换，不负责资源管理

**Xen 不将调度能力下放到 Domain 0 的原因**:
1. ✅ **可靠性**: Domain 0 崩溃不应导致系统崩溃
2. ✅ **隔离性**: 调度器必须独立于所有 Domain
3. ✅ **公平性**: Domain 0 不应有特殊优势
4. ✅ **安全性**: 调度器不能被任何 Domain 控制
5. ✅ **架构优势**: Type-1 Hypervisor 的核心特征

**核心原则**:
- **TF-A**: 简单性 = 安全性，单一职责
- **Xen**: 调度器必须在最稳定、最安全的层（Hypervisor）

这两个设计决策都遵循了各自系统的设计哲学和目标。

## 十、Xen 的 vCPU 管理

### 10.1 vCPU 数据结构

**位置**: `xen/xen/include/xen/sched.h:174`

vCPU 是 Xen 中虚拟 CPU 的抽象，每个 vCPU 代表一个虚拟处理器。

**核心数据结构**:

```174:307:xen/xen/include/xen/sched.h
struct vcpu
{
    int              vcpu_id;              // vCPU ID（在 Domain 内唯一）
    int              processor;             // 当前运行的物理 CPU
    struct domain   *domain;                // 所属 Domain
    struct vcpu     *next_in_list;         // Domain 内 vCPU 链表

    // 运行状态
    struct vcpu_runstate_info runstate;     // 运行状态信息
    unsigned int     new_state;            // 新状态（待更新）

    // 状态标志
    bool             is_initialised;       // 是否已初始化
    bool             is_running;           // 是否正在运行
    bool             is_urgent;            // 是否需要快速唤醒
    bool             force_context_switch;  // 是否需要强制上下文切换

    // 暂停机制
    unsigned long    pause_flags;          // 暂停标志位
    atomic_t         pause_count;          // 暂停计数
    atomic_t         vm_event_pause_count; // VM 事件暂停计数
    int              controller_pause_count; // 控制器暂停计数

    // Dirty CPU 跟踪
    unsigned int     dirty_cpu;             // 持有此 vCPU 状态的 CPU

    // 调度单元
    struct sched_unit *sched_unit;         // 调度单元（多个 vCPU 可组成一个单元）

    // 定时器
    struct timer     periodic_timer;       // 周期性定时器
    struct timer     singleshot_timer;     // 单次定时器
    struct timer     poll_timer;           // 轮询定时器

    // 架构特定数据
    struct arch_vcpu arch;                 // 架构特定信息

    // 其他资源
    struct guest_area vcpu_info_area;      // vCPU 信息区域
    struct waitqueue_vcpu *waitqueue_vcpu; // 等待队列
    // ... 更多字段 ...
};
```

### 10.2 vCPU 生命周期管理

#### 10.2.1 vCPU 创建

**位置**: `xen/xen/common/domain.c:224`

```224:306:xen/xen/common/domain.c
struct vcpu *vcpu_create(struct domain *d, unsigned int vcpu_id)
{
    struct vcpu *v;

    // 1. 参数检查
    if ( vcpu_id >= d->max_vcpus || d->vcpu[vcpu_id] ||
         (!is_idle_domain(d) && vcpu_id && !d->vcpu[vcpu_id - 1]) )
    {
        ASSERT_UNREACHABLE();
        return NULL;
    }

    // 2. 分配 vCPU 结构
    if ( (v = alloc_vcpu_struct(d)) == NULL )
        return NULL;

    // 3. 初始化基本字段
    v->domain = d;
    v->vcpu_id = vcpu_id;
    v->dirty_cpu = VCPU_CPU_CLEAN;

    // 4. 初始化锁和任务
    rwlock_init(&v->virq_lock);
    tasklet_init(&v->continue_hypercall_tasklet, NULL, NULL);

    // 5. 初始化授权表
    grant_table_init_vcpu(v);

    // 6. 设置初始状态
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

    // 7. 调度器初始化
    if ( sched_init_vcpu(v) != 0 )
        goto fail_wq;

    // 8. 架构特定初始化
    if ( arch_vcpu_create(v) != 0 )
        goto fail_sched;

    // 9. 添加到 Domain 的 vCPU 列表
    d->vcpu[vcpu_id] = v;
    if ( vcpu_id != 0 )
    {
        // 链接到前一个 vCPU
        int prev_id = v->vcpu_id - 1;
        while ( (prev_id >= 0) && (d->vcpu[prev_id] == NULL) )
            prev_id--;
        BUG_ON(prev_id < 0);
        v->next_in_list = d->vcpu[prev_id]->next_in_list;
        d->vcpu[prev_id]->next_in_list = v;
    }

    // 10. 检查关闭状态
    vcpu_check_shutdown(v);

    return v;

 fail_sched:
    sched_destroy_vcpu(v);
 fail_wq:
    destroy_waitqueue_vcpu(v);
    if ( vcpu_teardown(v) )
        ASSERT_UNREACHABLE();
    vcpu_destroy(v);
    return NULL;
}
```

**关键步骤**:

1. **参数验证**: 检查 vcpu_id 有效性，确保顺序分配
2. **结构分配**: 分配 vCPU 数据结构
3. **基本初始化**: 设置 domain、vcpu_id、dirty_cpu
4. **资源初始化**: 初始化锁、任务、授权表
5. **状态设置**:
   - 空闲域: `RUNSTATE_running`
   - 普通域: `RUNSTATE_offline` + `_VPF_down`
6. **调度器初始化**: 初始化调度器相关结构
7. **架构初始化**: 调用 `arch_vcpu_create()`（x86/ARM 特定）
8. **链表管理**: 将 vCPU 添加到 Domain 的 vCPU 链表

#### 10.2.2 vCPU 初始化 (VCPUOP_initialise)

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

**功能**: 初始化 vCPU 的架构特定状态

**参数**:
- **PV/ARM**: `vcpu_guest_context` - 包含寄存器、页表等初始状态
- **x86 HVM**: `vcpu_hvm_context` - 包含 VMCS/VMCB 初始状态

**关键点**:
- 每个 vCPU 只能初始化一次
- 初始化后 vCPU 不会自动运行
- 需要调用 `VCPUOP_up` 才能使 vCPU 可运行

#### 10.2.3 vCPU 上线 (VCPUOP_up)

**位置**: `xen/xen/common/domain.c:1833`

```1833:1850:xen/xen/common/domain.c
    case VCPUOP_up:
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

**步骤**:
1. 检查 vCPU 是否已初始化
2. 清除 `_VPF_down` 标志
3. 调用 `vcpu_wake()` 唤醒 vCPU

**唤醒后的状态转换**:
- `RUNSTATE_offline` → `RUNSTATE_runnable`
- vCPU 被加入调度器的运行队列

#### 10.2.4 vCPU 下线 (VCPUOP_down)

**位置**: `xen/xen/common/domain.c:1854`

```1854:1879:xen/xen/common/domain.c
    case VCPUOP_down:
        // 检查是否还有其他 vCPU 在线
        for_each_vcpu ( d, v )
            if ( v->vcpu_id != vcpuid && !test_bit(_VPF_down, &v->pause_flags) )
            {
               rc = 1;
               break;
            }

        if ( !rc ) /* 最后一个 vCPU 下线 */
        {
            domain_shutdown(d, SHUTDOWN_poweroff);
            break;
        }

        rc = 0;
        v = d->vcpu[vcpuid];

        if ( !test_and_set_bit(_VPF_down, &v->pause_flags) )
            vcpu_sleep_nosync(v);

        break;
```

**功能**: 使 vCPU 不可运行

**步骤**:
1. 检查是否为最后一个在线 vCPU
2. 如果是最后一个，关闭整个 Domain
3. 否则，设置 `_VPF_down` 标志
4. 调用 `vcpu_sleep_nosync()` 使 vCPU 进入睡眠

### 10.3 vCPU 状态管理

#### 10.3.1 运行状态 (Runstate)

**定义**: `xen/xen/include/public/vcpu.h:84-99`

```84:99:xen/xen/include/public/vcpu.h
/* VCPU is currently running on a physical CPU. */
#define RUNSTATE_running  0

/* VCPU is runnable, but not currently scheduled on any physical CPU. */
#define RUNSTATE_runnable 1

/* VCPU is blocked (a.k.a. idle). It is therefore not runnable. */
#define RUNSTATE_blocked  2

/*
 * VCPU is not runnable, but it is not blocked.
 * This is a 'catch all' state for things like hotplug and pauses by the
 * system administrator (or for critical sections in the hypervisor).
 * RUNSTATE_blocked dominates this state (it is the preferred state).
 */
#define RUNSTATE_offline  3
```

**状态转换图**:

```
RUNSTATE_offline (创建时)
    |
    v
VCPUOP_initialise (初始化)
    |
    v
VCPUOP_up (上线)
    |
    v
RUNSTATE_runnable (可运行)
    |
    v
[调度器选择] → RUNSTATE_running (运行中)
    |
    v
[时间片到期/阻塞] → RUNSTATE_runnable / RUNSTATE_blocked
    |
    v
VCPUOP_down (下线) → RUNSTATE_offline
```

#### 10.3.2 暂停标志 (Pause Flags)

**定义**: `xen/xen/include/xen/sched.h:920-955`

```920:955:xen/xen/include/xen/sched.h
/* VCPU is down (i.e., not running). */
#define _VPF_down           0
#define VPF_down            (1UL<<_VPF_down)
/* VCPU is blocked. */
#define _VPF_blocked        1
#define VPF_blocked         (1UL<<_VPF_blocked)
/* VCPU is paused. */
#define _VPF_paused         2
#define VPF_paused          (1UL<<_VPF_paused)
/* VCPU is yielding. */
#define _VPF_yielding       3
#define VPF_yielding        (1UL<<_VPF_yielding)
/* VCPU is waiting for an event. */
#define _VPF_waiting        4
#define VPF_waiting         (1UL<<_VPF_waiting)
/* VCPU is blocked due to missing mem_sharing ring. */
#define _VPF_mem_sharing    6
#define VPF_mem_sharing     (1UL<<_VPF_mem_sharing)
/* VCPU is being reset. */
#define _VPF_in_reset       7
#define VPF_in_reset        (1UL<<_VPF_in_reset)
/* VCPU is parked. */
#define _VPF_parked         8
#define VPF_parked          (1UL<<_VPF_parked)
```

**暂停机制**:

- **`pause_flags`**: 位标志，表示 vCPU 的暂停原因
- **`pause_count`**: 原子计数，支持嵌套暂停
- **`controller_pause_count`**: 系统控制器暂停计数（用于 Domain 创建）

**可运行性检查**:

```957:962:xen/xen/include/xen/sched.h
static inline bool vcpu_runnable(const struct vcpu *v)
{
    return !(v->pause_flags |
             atomic_read(&v->pause_count) |
             atomic_read(&v->domain->pause_count));
}
```

vCPU 可运行的条件：
- `pause_flags` 为 0
- `pause_count` 为 0
- Domain 的 `pause_count` 为 0

### 10.4 vCPU 调度管理

#### 10.4.1 调度单元 (Sched Unit)

**概念**: 多个 vCPU 可以组成一个调度单元，共享调度策略

**数据结构**:

```309:319:xen/xen/include/xen/sched.h
struct sched_unit {
    struct domain         *domain;
    struct vcpu           *vcpu_list;      // vCPU 列表
    void                  *priv;            // 调度器私有数据
    struct sched_unit     *next_in_list;
    struct sched_resource *res;            // 调度资源
    unsigned int           unit_id;

    bool                   is_running;      // 是否正在运行
    // ...
};
```

**调度单元的作用**:
- 多个 vCPU 可以共享一个调度单元（如 SMP guest）
- 调度器在单元级别进行调度决策
- 支持 vCPU 级别的细粒度控制

#### 10.4.2 vCPU 唤醒

**位置**: `xen/xen/common/sched/core.c:965`

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
        // 状态转换
        if ( v->runstate.state >= RUNSTATE_blocked )
            vcpu_runstate_change(v, RUNSTATE_runnable, NOW());

        // 通知调度器
        sched_wake(unit_scheduler(unit), unit);

        // 如果需要，强制上下文切换
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

**唤醒流程**:
1. 获取调度锁
2. 检查 vCPU 是否可运行
3. 状态转换: `RUNSTATE_blocked` → `RUNSTATE_runnable`
4. 通知调度器: `sched_wake()`
5. 如果需要，触发强制上下文切换

#### 10.4.3 vCPU 睡眠

**位置**: `xen/xen/common/sched/core.c` (通过 `vcpu_sleep_sync` 和 `vcpu_sleep_nosync`)

**两种模式**:
- **同步睡眠** (`vcpu_sleep_sync`): 等待 vCPU 进入睡眠状态
- **异步睡眠** (`vcpu_sleep_nosync`): 不等待，立即返回

**使用场景**:
- **同步**: Domain 暂停时需要确保所有 vCPU 已停止
- **异步**: 快速暂停，不等待 vCPU 停止

### 10.5 vCPU 同步机制

#### 10.5.1 vCPU 暂停/恢复

**vCPU 级别暂停**:

```1236:1253:xen/xen/common/domain.c
void vcpu_pause(struct vcpu *v)
{
    ASSERT(v != current);
    atomic_inc(&v->pause_count);
    vcpu_sleep_sync(v);
}

void vcpu_unpause(struct vcpu *v)
{
    if ( atomic_dec_and_test(&v->pause_count) )
        vcpu_wake(v);
}
```

**Domain 级别暂停**:

```1295:1331:xen/xen/common/domain.c
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

void domain_unpause(struct domain *d)
{
    struct vcpu *v;

    arch_domain_unpause(d);

    if ( atomic_dec_and_test(&d->pause_count) )
        for_each_vcpu( d, v )
            vcpu_wake(v);
}
```

**暂停计数机制**:
- 支持嵌套暂停（多次 pause 需要多次 unpause）
- 使用原子操作保证线程安全
- Domain 和 vCPU 都有独立的暂停计数

#### 10.5.2 Dirty CPU 跟踪

**目的**: 跟踪哪个物理 CPU 正在持有 vCPU 的状态

**定义**:

```248:250:xen/xen/include/xen/sched.h
/* The CPU, if any, which is holding onto this VCPU's state. */
#define VCPU_CPU_CLEAN (~0u)
unsigned int     dirty_cpu;
```

**使用场景**:
- 上下文切换时，需要知道 vCPU 的状态在哪个 CPU 上
- 如果状态在远程 CPU，需要先同步状态
- 用于优化上下文切换性能

**检查函数**:

```970:973:xen/xen/include/xen/sched.h
static inline bool vcpu_cpu_dirty(const struct vcpu *v)
{
    return is_vcpu_dirty_cpu(read_atomic(&v->dirty_cpu));
}
```

### 10.6 vCPU 管理总结

**vCPU 生命周期**:

```
创建 (vcpu_create)
    ↓
初始化 (VCPUOP_initialise)
    ↓
上线 (VCPUOP_up)
    ↓
可运行 (RUNSTATE_runnable)
    ↓
运行 (RUNSTATE_running)
    ↓
下线 (VCPUOP_down)
    ↓
销毁 (vcpu_destroy)
```

**vCPU 状态机**:

```
RUNSTATE_offline ←→ RUNSTATE_runnable ←→ RUNSTATE_running
       ↑                    ↓                    ↓
       └────────────────────┴────────────────────┘
                    (阻塞或暂停)
```

**关键管理机制**:

1. **创建管理**: 顺序分配，链表组织
2. **状态管理**: 运行状态 + 暂停标志
3. **调度管理**: 通过调度单元进行调度
4. **同步机制**: 暂停计数 + Dirty CPU 跟踪
5. **资源管理**: 定时器、等待队列、授权表等

**设计特点**:

- **层次化**: Domain → vCPU → 调度单元
- **状态驱动**: 通过状态机管理生命周期
- **同步安全**: 使用原子操作和锁保证线程安全
- **性能优化**: Dirty CPU 跟踪、异步操作等

## 十一、Xen Trace 分析工具的两级管理数据结构

### 11.1 概述

在 Xen 的 trace 分析工具 (`xen/tools/xentrace/xenalyze.c`) 中，使用了 `struct domain_data` 和 `struct vcpu_data` 两级管理结构来组织和分析 trace 数据。

**设计目的**:
- 跟踪和分析 Xen Hypervisor 的运行行为
- 统计 Domain 和 vCPU 的性能指标
- 支持性能分析和问题诊断

**两级关系**:
```
domain_data (Domain 级别)
    ├── vcpu_data[0] (vCPU 0)
    ├── vcpu_data[1] (vCPU 1)
    └── ...
```

### 11.2 struct domain_data 结构

**位置**: `xen/tools/xentrace/xenalyze.c:1711`

```1711:1743:xen/tools/xentrace/xenalyze.c
struct domain_data {
    struct domain_data *next;              // 链表指针
    int did;                                // Domain ID
    struct vcpu_data *vcpu[MAX_CPUS];      // vCPU 数组

    int max_vid;                            // 最大 vCPU ID

    // Domain 运行状态
    int runstate;                           // 当前运行状态
    tsc_t runstate_tsc;                    // 状态切换时间戳
    struct cycle_summary total_time;       // 总时间统计
    struct cycle_summary runstates[DOMAIN_RUNSTATE_MAX]; // 各状态时间统计

    // CR3 跟踪（页表基址）
    struct cr3_value_struct *cr3_value_head;

    // EIP 跟踪
    struct eip_list_struct *emulate_eip_list;    // 模拟 EIP 列表
    struct eip_list_struct *interrupt_eip_list; // 中断 EIP 列表

    // Guest 中断统计
    int guest_interrupt[GUEST_INTERRUPT_MAX+1];

    // HVM 摘要
    struct hvm_short_summary_struct hvm_short;

    // 内存操作统计
    struct {
        int done[MEM_MAX];                  // 完成的操作数
        int done_interval[MEM_MAX];         // 间隔内完成的操作数
        int done_for[MEM_MAX];              // 为其他 Domain 完成的操作数
        int done_for_interval[MEM_MAX];     // 间隔内为其他 Domain 完成的操作数
    } memops;

    // POD (Populate on Demand) 统计
    struct {
        int reclaim_order[POD_ORDER_MAX];   // 回收顺序统计
        int reclaim_context[POD_RECLAIM_CONTEXT_MAX]; // 回收上下文统计
        int reclaim_context_order[POD_RECLAIM_CONTEXT_MAX][POD_ORDER_MAX];
        int populate_order[POD_ORDER_MAX];  // 填充顺序统计
    } pod;
};
```

**Domain 运行状态**:

```1675:1694:xen/tools/xentrace/xenalyze.c
enum {
    DOMAIN_RUNSTATE_BLOCKED=0,              // 完全阻塞
    DOMAIN_RUNSTATE_PARTIAL_RUN,            // 部分运行
    DOMAIN_RUNSTATE_FULL_RUN,              // 完全运行
    DOMAIN_RUNSTATE_PARTIAL_CONTENTION,     // 部分竞争
    DOMAIN_RUNSTATE_CONCURRENCY_HAZARD,    // 并发风险
    DOMAIN_RUNSTATE_FULL_CONTENTION,       // 完全竞争
    DOMAIN_RUNSTATE_LOST,                   // 丢失记录
    DOMAIN_RUNSTATE_MAX
};
```

**关键字段说明**:
- **`vcpu[MAX_CPUS]`**: vCPU 数组，每个 Domain 最多 `MAX_CPUS` 个 vCPU
- **`runstates[]`**: Domain 级别的运行状态统计
- **`cr3_value_head`**: CR3 值链表，跟踪页表切换
- **`memops`**: 内存操作统计（grant map/unmap、P2M 操作等）
- **`pod`**: POD 内存管理统计

### 11.3 struct vcpu_data 结构

**位置**: `xen/tools/xentrace/xenalyze.c:1625`

```1625:1673:xen/tools/xentrace/xenalyze.c
struct vcpu_data {
    int vid;                                // vCPU ID
    struct domain_data *d;                  // 指向所属 Domain（up-pointer）
    unsigned activated:1, delayed_init:1;  // 激活标志、延迟初始化标志

    int guest_paging_levels;                // Guest 分页级别

    // 调度信息
    struct {
        int state;                          // 当前运行状态
        int runnable_state;                 // 可运行状态（仅在 state==RUNSTATE_RUNNABLE 时有效）
        tsc_t tsc;                          // 时间戳
        // TSC 偏差检测/校正
        struct last_oldstate_struct {
            int wrong, actual, pid;
            tsc_t tsc;
        } last_oldstate;
        // 性能计数器
        unsigned long long p1_start, p2_start;
    } runstate;

    struct pcpu_info *p;                    // 当前运行的物理 CPU
    tsc_t pcpu_tsc;                         // 物理 CPU 时间戳

    // 硬件跟踪
    struct {
        long long val;                      // CR3 值
        tsc_t start_time;                   // 开始时间
        struct cr3_value_struct *data;      // CR3 数据链表
    } cr3;

    // IPI 延迟跟踪
    struct vlapic_struct vlapic;

    // 摘要信息
    struct cycle_framework f;               // 周期框架
    struct cycle_summary runstates[RUNSTATE_MAX]; // 各运行状态时间统计
    struct cycle_summary runnable_states[RUNNABLE_STATE_MAX]; // 可运行状态统计
    struct cycle_summary cpu_affinity_all, // CPU 亲和性统计（全部）
        cpu_affinity_pcpu[MAX_CPUS];       // CPU 亲和性统计（每个 CPU）

    // 数据类型（HVM/PV）
    enum {
        VCPU_DATA_NONE=0,
        VCPU_DATA_HVM,
        VCPU_DATA_PV
    } data_type;

    // 联合体：HVM 或 PV 特定数据
    union {
        struct hvm_data hvm;
        struct pv_data pv;
    };
};
```

**vCPU 运行状态**:

```1510:1540:xen/tools/xentrace/xenalyze.c
enum {
    RUNSTATE_RUNNING=0,                     // 正在运行
    RUNSTATE_RUNNABLE,                      // 可运行
    RUNSTATE_BLOCKED,                       // 阻塞
    RUNSTATE_OFFLINE,                       // 离线
    RUNSTATE_LOST,                          // 丢失
    RUNSTATE_QUEUED,                        // 排队
    RUNSTATE_INIT,                          // 初始化
    RUNSTATE_MAX
};
```

**关键字段说明**:
- **`d`**: 指向所属 Domain 的指针（up-pointer）
- **`runstate`**: vCPU 运行状态和统计
- **`cr3`**: CR3（页表基址）跟踪
- **`cpu_affinity_pcpu[]`**: 每个物理 CPU 的亲和性统计
- **`data_type`**: 虚拟化类型（HVM/PV）
- **`hvm`/`pv`**: 虚拟化类型特定的数据

### 11.4 两级管理关系

#### 11.4.1 层次结构

```
domain_list (全局链表)
    ├── domain_data (did=0)
    │   ├── vcpu_data[0] (vid=0)
    │   ├── vcpu_data[1] (vid=1)
    │   └── ...
    ├── domain_data (did=1)
    │   ├── vcpu_data[0] (vid=0)
    │   └── ...
    └── ...
```

#### 11.4.2 双向引用

**Domain → vCPU**:
- `domain_data.vcpu[MAX_CPUS]`: Domain 包含 vCPU 数组
- 通过 `d->vcpu[vid]` 访问特定 vCPU

**vCPU → Domain**:
- `vcpu_data.d`: vCPU 包含指向 Domain 的指针
- 通过 `v->d` 访问所属 Domain

**示例**:
```c
// 从 Domain 访问 vCPU
struct domain_data *d = domain_find(0);
struct vcpu_data *v = d->vcpu[0];

// 从 vCPU 访问 Domain
struct vcpu_data *v = vcpu_find(0, 0);
struct domain_data *d = v->d;
```

### 11.5 创建和管理函数

#### 11.5.1 Domain 创建

**位置**: `xen/tools/xentrace/xenalyze.c:6724`

```6724:6763:xen/tools/xentrace/xenalyze.c
struct domain_data * domain_create(int did)
{
    struct domain_data *d;

    fprintf(warn, "Creating domain %d\n", did);

    if((d=malloc(sizeof(*d)))==NULL)
    {
        fprintf(stderr, "%s: malloc %zd failed!\n", __func__, sizeof(*d));
        error(ERR_SYSTEM, NULL);
    }

    /* Initialize domain & vcpus */
    domain_init(d, did);

    return d;
}

struct domain_data * domain_find(int did)
{
    struct domain_data *d, *n, **q;

    /* Look for domain, keeping track of the last pointer so we can add
       a domain if we need to. */
    for ( d = domain_list, q=&domain_list ;
          d && (d->did < did) ;
          q = &d->next, d=d->next ) ;

    if(d && d->did == did)
        return d;

    /* Make a new domain */
    n = domain_create(did);

    /* Insert it into the list */
    n->next = d;
    *q = n;

    return n;
}
```

**特点**:
- 使用链表组织 Domain（按 `did` 排序）
- `domain_find()` 自动创建不存在的 Domain
- 插入时保持链表有序

#### 11.5.2 vCPU 创建

**位置**: `xen/tools/xentrace/xenalyze.c:6680`

```6680:6778:xen/tools/xentrace/xenalyze.c
struct vcpu_data * vcpu_create(struct domain_data *d, int vid)
{
    struct vcpu_data *v;

    assert(d->vcpu[vid] == NULL);

    fprintf(warn, "Creating vcpu %d for dom %d\n", vid, d->did);

    if((v=malloc(sizeof(*v)))==NULL)
    {
        fprintf(stderr, "%s: malloc %zd failed!\n", __func__, sizeof(*d));
        error(ERR_SYSTEM, NULL);
    }

    bzero(v, sizeof(*v));

    v->vid = vid;
    v->d = d;
    v->p = NULL;
    v->runstate.state = RUNSTATE_INIT;
    v->runstate.last_oldstate.wrong = RUNSTATE_INIT;

    d->vcpu[vid] = v;

    assert(v == v->d->vcpu[v->vid]);

    if(vid > d->max_vid)
        d->max_vid = vid;

    return v;
}

struct vcpu_data * vcpu_find(int did, int vid)
{
    struct domain_data *d;
    struct vcpu_data *v;

    d = domain_find(did);

    v = d->vcpu[vid];

    if(!v)
        v = vcpu_create(d, vid);

    return v;
}
```

**特点**:
- vCPU 属于特定 Domain
- 设置双向引用：`v->d = d` 和 `d->vcpu[vid] = v`
- `vcpu_find()` 自动创建不存在的 vCPU
- 更新 Domain 的 `max_vid`

### 11.6 统计和摘要功能

#### 11.6.1 Domain 摘要

**位置**: `xen/tools/xentrace/xenalyze.c:9966`

```9966:10009:xen/tools/xentrace/xenalyze.c
void domain_summary(void)
{
    struct domain_data * d;
    int i;

    if(opt.show_default_domain_summary) {
        d = &default_domain;
        printf("|-- Default domain --|\n");

        for( i = 0; i < MAX_CPUS ; i++ )
        {
            if(d->vcpu[i])
                vcpu_summary(d->vcpu[i]);
        }
    }

    for ( d = domain_list ; d ; d=d->next )
    {
        int i;
        printf("|-- Domain %d --|\n", d->did);

        sched_summary_domain(d);

        mem_summary_domain(d);

        for( i = 0; i < MAX_CPUS ; i++ )
        {
            if(d->vcpu[i])
                vcpu_summary(d->vcpu[i]);
        }

        printf("Emulate eip list\n");
        dump_eip(d->emulate_eip_list);

        if ( opt.with_interrupt_eip_enumeration )
        {
            printf("Interrupt eip list (vector %d)\n",
                   opt.interrupt_eip_enumeration_vector);
            dump_eip(d->interrupt_eip_list);
        }

        cr3_dump_list(d->cr3_value_head);
    }
}
```

**输出内容**:
- Domain 运行状态统计
- 内存操作统计
- 所有 vCPU 的摘要
- EIP 列表（模拟和中断）
- CR3 值列表

#### 11.6.2 vCPU 摘要

**位置**: `xen/tools/xentrace/xenalyze.c:9950`

```9950:9964:xen/tools/xentrace/xenalyze.c
void vcpu_summary(struct vcpu_data *v)
{
    printf("-- v%d --\n", v->vid);
    sched_summary_vcpu(v);
    switch(v->data_type) {
    case VCPU_DATA_HVM:
        hvm_summary(&v->hvm);
        break;
    case VCPU_DATA_PV:
        pv_summary(&v->pv);
        break;
    default:
        break;
    }
}
```

**输出内容**:
- vCPU 运行状态统计
- CPU 亲和性统计
- HVM 或 PV 特定统计

#### 11.6.3 调度摘要

**Domain 级别**:

```7484:7494:xen/tools/xentrace/xenalyze.c
void sched_summary_domain(struct domain_data *d)
{
    int i;
    char desc[30];

    printf(" Runstates:\n");
    for(i=0; i<DOMAIN_RUNSTATE_MAX; i++) {
        snprintf(desc,30, "  %8s", domain_runstate_name[i]);
        print_cycle_summary(d->runstates+i, desc);
    }
}
```

**vCPU 级别**:

```7449:7482:xen/tools/xentrace/xenalyze.c
void sched_summary_vcpu(struct vcpu_data *v)
{
    int i;
    char desc[30];

    /* FIXME: Update all records like this */
    if ( v->pcpu_tsc )
    {
        update_cycles(&v->cpu_affinity_all, P.f.last_tsc - v->pcpu_tsc);
        update_cycles(&v->cpu_affinity_pcpu[v->p->pid], P.f.last_tsc - v->pcpu_tsc);
    }

    printf(" Runstates:\n");
    for(i=0; i<RUNSTATE_MAX; i++) {
        snprintf(desc,30, "  %8s", runstate_name[i]);
        print_cycle_summary(v->runstates+i, desc);
        if ( i==RUNSTATE_RUNNABLE )
        {
            int j;
            for(j=0; j<RUNNABLE_STATE_MAX; j++) {
                if ( j == RUNNABLE_STATE_INVALID )
                    continue;
                snprintf(desc,30, "    %8s", runnable_state_name[j]);
                print_cycle_summary(v->runnable_states+j, desc);
            }
        }
    }
    print_cpu_affinity(&v->cpu_affinity_all, " cpu affinity");
    for ( i = 0; i < MAX_CPUS ; i++)
    {
        snprintf(desc,30, "   [%d]", i);
        print_cpu_affinity(v->cpu_affinity_pcpu+i, desc);
    }
}
```

### 11.7 与 Hypervisor 数据结构的关系

**对比**:

| Trace 工具 | Hypervisor |
|-----------|-----------|
| `struct domain_data` | `struct domain` |
| `struct vcpu_data` | `struct vcpu` |
| `struct pcpu_info` | 物理 CPU 信息 |

**区别**:
- **Hypervisor**: 运行时数据结构，用于管理 Domain 和 vCPU
- **Trace 工具**: 分析数据结构，用于统计和分析 trace 数据

**关系**:
- Trace 工具通过解析 trace 记录来填充 `domain_data` 和 `vcpu_data`
- 这些结构反映了 Hypervisor 的运行行为
- 用于性能分析和问题诊断

### 11.8 使用场景

**主要用途**:
1. **性能分析**: 统计 Domain 和 vCPU 的运行时间
2. **问题诊断**: 分析调度、内存操作、中断等事件
3. **资源监控**: 跟踪 CPU 亲和性、内存使用等
4. **优化建议**: 基于统计数据提供优化建议

**典型流程**:
```
1. 收集 trace 数据 (xentrace)
2. 解析 trace 文件 (xenalyze)
3. 填充 domain_data 和 vcpu_data
4. 生成统计报告
5. 分析性能问题
```

### 11.9 总结

**两级管理结构特点**:
- **层次化**: Domain → vCPU 两级管理
- **双向引用**: Domain 包含 vCPU 数组，vCPU 指向 Domain
- **统计丰富**: 包含运行状态、内存操作、中断等统计
- **类型支持**: 支持 HVM 和 PV 两种虚拟化类型

**设计优势**:
- **清晰的组织**: 两级结构清晰反映 Domain-vCPU 关系
- **灵活扩展**: 易于添加新的统计项
- **高效访问**: 双向引用支持快速访问
- **完整统计**: 覆盖调度、内存、中断等多个维度

## 十二、Hypervisor 中的 Domain → vCPU 两级管理

### 12.1 概述

Xen Hypervisor 使用两级管理结构来组织 Domain 和 vCPU：
- **Domain 级别**: 管理虚拟机的整体资源和策略
- **vCPU 级别**: 管理虚拟 CPU 的执行状态和调度

这种两级管理结构提供了清晰的层次关系，支持高效的访问和遍历。

### 12.2 数据结构定义

#### 12.2.1 Domain 结构中的 vCPU 数组

**位置**: `xen/xen/include/xen/domain.h` (Domain 结构定义)

Domain 结构包含一个 vCPU 指针数组：

```c
struct domain {
    // ...
    struct vcpu *vcpu[];        // vCPU 指针数组
    unsigned int max_vcpus;     // 最大 vCPU 数量
    // ...
};
```

**关键特点**:
- **动态分配**: `d->vcpu` 在 Domain 创建时动态分配
- **大小限制**: `max_vcpus` 限制数组大小
- **顺序分配**: vCPU 必须按顺序分配（0, 1, 2, ...）

#### 12.2.2 vCPU 结构中的 Domain 指针

**位置**: `xen/xen/include/xen/sched.h:174`

```174:184:xen/xen/include/xen/sched.h
struct vcpu
{
    int              vcpu_id;

    int              processor;

    struct guest_area vcpu_info_area;

    struct domain   *domain;

    struct vcpu     *next_in_list;
```

**关键字段**:
- **`domain`**: 指向所属 Domain 的指针（up-pointer）
- **`vcpu_id`**: vCPU 在 Domain 内的 ID
- **`next_in_list`**: 链表指针，用于遍历 Domain 的所有 vCPU

### 12.3 双向引用关系

#### 12.3.1 Domain → vCPU（数组访问）

**数组索引访问**:
```c
struct domain *d = ...;
struct vcpu *v = d->vcpu[vcpu_id];
```

**特点**:
- O(1) 时间复杂度
- 通过 `vcpu_id` 直接访问
- 需要确保 `vcpu_id < max_vcpus`

**安全访问函数**:

```1014:1020:xen/xen/include/xen/sched.h
static inline struct vcpu *domain_vcpu(const struct domain *d,
                                       unsigned int vcpu_id)
{
    unsigned int idx = array_index_nospec(vcpu_id, d->max_vcpus);

    return vcpu_id >= d->max_vcpus ? NULL : d->vcpu[idx];
}
```

**功能**:
- 防止数组越界访问
- 使用 `array_index_nospec` 防止 Spectre 攻击
- 返回 NULL 如果 `vcpu_id` 无效

#### 12.3.2 vCPU → Domain（指针访问）

**直接指针访问**:
```c
struct vcpu *v = ...;
struct domain *d = v->domain;
```

**特点**:
- O(1) 时间复杂度
- 每个 vCPU 都有指向 Domain 的指针
- 支持快速访问 Domain 级别的资源

**使用场景**:
- 访问 Domain 的配置和策略
- 检查 Domain 的状态（暂停、关闭等）
- 访问 Domain 级别的资源（内存、中断等）

### 12.4 vCPU 链表组织

#### 12.4.1 链表结构

vCPU 通过 `next_in_list` 字段组织成链表：

```
d->vcpu[0] → vcpu[1] → vcpu[2] → ... → NULL
```

**链表特点**:
- **顺序链接**: 按 `vcpu_id` 顺序链接
- **从 vcpu[0] 开始**: 链表头是 `d->vcpu[0]`
- **支持遍历**: 通过 `for_each_vcpu` 宏遍历

#### 12.4.2 链表构建

**位置**: `xen/xen/common/domain.c:278`

```278:287:xen/xen/common/domain.c
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
```

**构建逻辑**:
1. 将新 vCPU 放入数组：`d->vcpu[vcpu_id] = v`
2. 如果不是第一个 vCPU（`vcpu_id != 0`）：
   - 找到前一个已分配的 vCPU（`prev_id`）
   - 将新 vCPU 插入到链表中
   - 更新前一个 vCPU 的 `next_in_list`

**示例**:
```
初始状态: d->vcpu[0] → NULL

创建 vcpu[0]:
  d->vcpu[0] = v0
  v0->next_in_list = NULL

创建 vcpu[1]:
  d->vcpu[1] = v1
  prev_id = 0
  v1->next_in_list = v0->next_in_list (NULL)
  v0->next_in_list = v1
  结果: v0 → v1 → NULL

创建 vcpu[2]:
  d->vcpu[2] = v2
  prev_id = 1
  v2->next_in_list = v1->next_in_list (NULL)
  v1->next_in_list = v2
  结果: v0 → v1 → v2 → NULL
```

### 12.5 遍历机制

#### 12.5.1 for_each_vcpu 宏

**定义**: `xen/xen/include/xen/sched.h:921`

```921:924:xen/xen/include/xen/sched.h
#define for_each_vcpu(_d,_v)                    \
 for ( (_v) = (_d)->vcpu ? (_d)->vcpu[0] : NULL; \
       (_v) != NULL;                            \
       (_v) = (_v)->next_in_list )
```

**使用示例**:
```c
struct domain *d = ...;
struct vcpu *v;

for_each_vcpu(d, v) {
    // 处理每个 vCPU
    printf("vCPU %d\n", v->vcpu_id);
}
```

**特点**:
- **遍历所有 vCPU**: 从 `vcpu[0]` 开始，通过链表遍历
- **空安全**: 检查 `d->vcpu` 是否为 NULL
- **顺序遍历**: 按 `vcpu_id` 顺序遍历

#### 12.5.2 遍历使用场景

**Domain 暂停所有 vCPU**:

```1295:1306:xen/xen/common/domain.c
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
```

**Domain 恢复所有 vCPU**:

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

### 12.6 Domain 创建时的 vCPU 数组分配

**位置**: `xen/xen/common/domain.c:665`

```665:677:xen/xen/common/domain.c
    /*
     * Allocate d->vcpu[] and set ->max_vcpus up early.  Various per-domain
     * resources want to be sized based on max_vcpus.
     */
    if ( !is_system_domain(d) )
    {
        err = -ENOMEM;
        d->vcpu = xzalloc_array(struct vcpu *, config->max_vcpus);
        if ( !d->vcpu )
            goto fail;

        d->max_vcpus = config->max_vcpus;
    }
```

**关键步骤**:
1. **分配数组**: 使用 `xzalloc_array` 分配 `max_vcpus` 个指针
2. **初始化为 NULL**: `xzalloc_array` 会将所有元素初始化为 NULL
3. **设置大小**: 设置 `d->max_vcpus`

**时机**:
- 在 Domain 创建早期分配
- 其他资源（如授权表）需要根据 `max_vcpus` 调整大小

### 12.7 vCPU 创建时的双向引用设置

**位置**: `xen/xen/common/domain.c:224`

```224:292:xen/xen/common/domain.c
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

    // ... 初始化其他字段 ...

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
}
```

**关键步骤**:
1. **参数检查**: 验证 `vcpu_id` 有效且未分配
2. **分配结构**: 分配 vCPU 结构
3. **设置双向引用**:
   - `v->domain = d` (vCPU → Domain)
   - `d->vcpu[vcpu_id] = v` (Domain → vCPU)
4. **构建链表**: 将新 vCPU 插入链表
5. **可见性**: 确保 vCPU 对 `for_each_vcpu` 可见

### 12.8 访问模式总结

#### 12.8.1 从 Domain 访问 vCPU

**方式 1: 数组索引（O(1)）**
```c
struct vcpu *v = d->vcpu[vcpu_id];
```

**方式 2: 安全函数（推荐）**
```c
struct vcpu *v = domain_vcpu(d, vcpu_id);
```

**方式 3: 遍历所有 vCPU**
```c
struct vcpu *v;
for_each_vcpu(d, v) {
    // 处理 vCPU
}
```

#### 12.8.2 从 vCPU 访问 Domain

**直接指针访问（O(1)）**
```c
struct domain *d = v->domain;
```

**使用场景**:
- 检查 Domain 状态：`v->domain->is_shutting_down`
- 访问 Domain 资源：`v->domain->grant_table`
- 检查 Domain 暂停：`atomic_read(&v->domain->pause_count)`

### 12.9 设计原则和约束

#### 12.9.1 顺序分配约束

**约束**:
- vCPU 必须按顺序分配（0, 1, 2, ...）
- 不能跳过 vCPU ID
- 空闲域（idle domain）例外

**原因**:
- 简化链表构建
- 保证 `for_each_vcpu` 的正确性
- 简化资源管理

**检查代码**:
```235:240:xen/xen/common/domain.c
    if ( vcpu_id >= d->max_vcpus || d->vcpu[vcpu_id] ||
         (!is_idle_domain(d) && vcpu_id && !d->vcpu[vcpu_id - 1]) )
    {
        ASSERT_UNREACHABLE();
        return NULL;
    }
```

#### 12.9.2 一致性保证

**注释说明**: `xen/xen/include/xen/sched.h:997`

```997:1013:xen/xen/include/xen/sched.h
/*
 * For each allocated vcpu, d->vcpu[X]->vcpu_id == X
 *
 * During construction, all vcpus in d->vcpu[] are allocated sequentially, and
 * in ascending order.  Therefore, if d->vcpu[N] exists (e.g. derived from
 * current), all vcpus with an id less than N also exist.
 *
 * SMP considerations: The idle domain is constructed before APs are started.
 * All other domains have d->vcpu[] allocated and d->max_vcpus set before the
 * domain is made visible in the domlist, which is serialised on the global
 * domlist_update_lock.
 *
 * Therefore, all observations of d->max_vcpus vs d->vcpu[] will be consistent
 * despite the lack of smp_* barriers, either by being on the same CPU as the
 * one which issued the writes, or because of barrier properties of the domain
 * having been inserted into the domlist.
 */
```

**关键保证**:
- **ID 一致性**: `d->vcpu[X]->vcpu_id == X`
- **顺序性**: vCPU 按升序分配
- **完整性**: 如果 `vcpu[N]` 存在，所有小于 N 的 vCPU 也存在
- **SMP 安全**: 通过 `domlist_update_lock` 保证一致性

### 12.10 两级管理的优势

#### 12.10.1 性能优势

- **O(1) 访问**: 通过数组索引直接访问 vCPU
- **高效遍历**: 链表遍历避免空指针检查
- **缓存友好**: 数组访问具有良好的缓存局部性

#### 12.10.2 设计优势

- **清晰层次**: Domain → vCPU 两级结构清晰
- **双向引用**: 支持双向快速访问
- **灵活扩展**: 易于添加新的管理功能

#### 12.10.3 安全性优势

- **边界检查**: `domain_vcpu` 提供安全访问
- **Spectre 防护**: 使用 `array_index_nospec`
- **一致性保证**: 通过锁和顺序分配保证一致性

### 12.11 实际应用示例

#### 12.11.1 Domain 关闭时清理所有 vCPU

```c
void domain_destroy(struct domain *d)
{
    struct vcpu *v;

    // 遍历所有 vCPU 并销毁
    for_each_vcpu(d, v) {
        vcpu_destroy(v);
    }

    // 释放 vCPU 数组
    xfree(d->vcpu);
    d->vcpu = NULL;
}
```

#### 12.11.2 检查 Domain 是否有运行中的 vCPU

```c
bool domain_has_running_vcpu(struct domain *d)
{
    struct vcpu *v;

    for_each_vcpu(d, v) {
        if (v->is_running)
            return true;
    }

    return false;
}
```

#### 12.11.3 从 vCPU 访问 Domain 资源

```c
void vcpu_access_domain_resource(struct vcpu *v)
{
    struct domain *d = v->domain;

    // 访问 Domain 级别的资源
    if (d->is_shutting_down) {
        // Domain 正在关闭
        return;
    }

    // 访问 Domain 的授权表
    grant_table_t *gt = d->grant_table;
    // ...
}
```

### 12.12 总结

**两级管理结构**:
```
Domain (domain)
    ├── vcpu[] (数组，O(1) 访问)
    │   ├── vcpu[0] → vcpu[1] → vcpu[2] → ... (链表，顺序遍历)
    │   └── ...
    └── max_vcpus (数组大小限制)
```

**关键特性**:
- **双向引用**: Domain ↔ vCPU
- **数组 + 链表**: 数组用于快速访问，链表用于遍历
- **顺序分配**: 保证一致性和简化管理
- **安全访问**: 提供边界检查和 Spectre 防护

**设计原则**:
- **性能优先**: O(1) 访问，高效遍历
- **安全可靠**: 边界检查，一致性保证
- **清晰简单**: 两级结构，易于理解

## 十三、利用 ARM TxSZ 实现高特权 Monitor

### 13.1 概述

在 ARM 架构中，**TxSZ (Translation Size)** 是 TCR (Translation Control Register) 中的一个字段，用于控制虚拟地址空间的大小。通过合理配置 TxSZ，可以在 Xen Hypervisor 中实现一个高特权的 Monitor 层，提供额外的安全隔离和监控能力。

**设计目标**:
- 创建一个比普通 Domain 更高特权的监控层
- 利用 TxSZ 限制地址空间，增强安全性
- 提供对 Hypervisor 和 Guest 的监控能力
- 实现安全隔离和访问控制

### 13.2 ARM TxSZ 基础

#### 13.2.1 TxSZ 的作用

**定义**:
- **T0SZ**: TCR 中的 T0 Size 字段，控制地址空间大小
- **地址空间大小**: `VA_bits = 64 - T0SZ` (ARM64)
- **表级别**: TxSZ 影响页表遍历的起始级别

**当前 Xen 中的使用**:

```1661:1744:xen/xen/arch/arm/mmu/p2m.c
void __init setup_virt_paging(void)
{
    /* Setup Stage 2 address translation */
    register_t val = VTCR_RES1|VTCR_SH0_IS|VTCR_ORGN0_WBWA|VTCR_IRGN0_WBWA;

    static const struct {
        unsigned int pabits; /* Physical Address Size */
        unsigned int t0sz;   /* Desired T0SZ, minimum in comment */
        unsigned int root_order; /* Page order of the root of the p2m */
        unsigned int sl0;    /* Desired SL0, maximum in comment */
    } pa_range_info[] __initconst = {
        /* T0SZ minimum and SL0 maximum from ARM DDI 0487H.a Table D5-6 */
        /*      PA size, t0sz(min), root-order, sl0(max) */
#ifdef CONFIG_ARM_64
        [0] = { 32,      32/*32*/,  0,          1 },
        [1] = { 36,      28/*28*/,  0,          1 },
        [2] = { 40,      24/*24*/,  1,          1 },
        [3] = { 42,      22/*22*/,  3,          1 },
        [4] = { 44,      20/*20*/,  0,          2 },
        [5] = { 48,      16/*16*/,  0,          2 },
        [6] = { 52,      12/*12*/,  4,          2 },
        [7] = { 0 }  /* Invalid */
#else
        { 32,      0/*0*/,    0,          1 },
        { 40,      24/*24*/,  1,          1 }
#endif
    };

    // ... 选择 pa_range ...

    val |= VTCR_SL0(pa_range_info[pa_range].sl0);
    val |= VTCR_T0SZ(pa_range_info[pa_range].t0sz);

    // ...
}
```

**关键点**:
- Xen 使用 `VTCR_T0SZ` 配置 Stage-2 转换的地址空间大小
- T0SZ 值越大，地址空间越小，安全性越高
- 不同的物理地址大小对应不同的 T0SZ 值

### 13.3 Monitor 架构设计

#### 13.3.1 架构层次

```
┌─────────────────────────────────────┐
│   EL3: Secure Monitor (可选)        │
│   - 固件验证                         │
│   - Secure/Non-Secure 切换          │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   EL2: Xen Hypervisor               │
│   ┌───────────────────────────────┐ │
│   │ Monitor Domain (高特权)        │ │
│   │ - 受限地址空间 (小 TxSZ)      │ │
│   │ - 监控能力                    │ │
│   │ - 安全隔离                    │ │
│   └───────────────────────────────┘ │
│   ┌───────────────────────────────┐ │
│   │ Domain 0 (特权域)             │ │
│   └───────────────────────────────┘ │
│   ┌───────────────────────────────┐ │
│   │ Domain U (普通域)              │ │
│   └───────────────────────────────┘ │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   EL1/EL0: Guest OS                 │
└─────────────────────────────────────┘
```

#### 13.3.2 Monitor Domain 特性

**高特权特性**:
- **受限地址空间**: 使用较小的 TxSZ，限制可访问的地址范围
- **监控能力**: 可以监控其他 Domain 的行为
- **安全隔离**: 独立的页表和地址空间
- **访问控制**: 可以控制对其他 Domain 的访问

### 13.4 实现方案

#### 13.4.1 创建 Monitor Domain

**步骤 1: 定义 Monitor Domain 结构**

```c
// xen/include/xen/monitor.h

struct monitor_domain {
    struct domain *domain;          // Monitor Domain 本身
    unsigned int t0sz;              // 受限的 T0SZ 值
    unsigned int va_bits;            // 虚拟地址位数 (64 - t0sz)

    // 监控能力
    struct {
        bool enabled;
        cpumask_t monitored_domains; // 被监控的 Domain
        struct list_head event_list;  // 监控事件列表
    } monitoring;

    // 安全策略
    struct {
        bool restrict_memory_access;
        bool restrict_hypercall;
        bool restrict_interrupt;
    } policy;
};
```

**步骤 2: 创建 Monitor Domain**

```c
// xen/arch/arm/monitor.c

static struct monitor_domain *monitor_domain = NULL;

int monitor_domain_create(void)
{
    struct domain *d;
    struct monitor_domain *md;
    struct xen_domctl_createdomain create;
    int rc;

    // 1. 创建 Domain
    memset(&create, 0, sizeof(create));
    create.flags = XEN_DOMCTL_CDF_privileged | XEN_DOMCTL_CDF_monitor;
    create.max_vcpus = 1;
    create.max_evtchn_port = 1024;
    create.max_grant_frames = 32;
    create.max_maptrack_frames = 1024;

    // 2. 设置受限的地址空间
    // 使用较大的 T0SZ 值，限制地址空间
    // 例如：T0SZ = 32，VA_bits = 32 (4GB 地址空间)
    create.arch.monitor_t0sz = 32;

    rc = domain_create(DOMID_INVALID, &create, &d);
    if ( rc )
        return rc;

    // 3. 分配 Monitor Domain 结构
    md = xzalloc(struct monitor_domain);
    if ( !md )
    {
        domain_destroy(d);
        return -ENOMEM;
    }

    md->domain = d;
    md->t0sz = create.arch.monitor_t0sz;
    md->va_bits = 64 - md->t0sz;

    // 4. 初始化监控能力
    md->monitoring.enabled = false;
    cpumask_clear(&md->monitoring.monitored_domains);
    INIT_LIST_HEAD(&md->monitoring.event_list);

    // 5. 设置安全策略
    md->policy.restrict_memory_access = true;
    md->policy.restrict_hypercall = true;
    md->policy.restrict_interrupt = true;

    monitor_domain = md;

    return 0;
}
```

#### 13.4.2 配置受限地址空间

**步骤 3: 设置 Monitor Domain 的 TCR**

```c
// xen/arch/arm/monitor.c

int monitor_setup_address_space(struct monitor_domain *md)
{
    struct vcpu *v;
    register_t tcr_el1;

    if ( !md || !md->domain )
        return -EINVAL;

    // 为 Monitor Domain 的每个 vCPU 配置 TCR
    for_each_vcpu(md->domain, v)
    {
        // 读取当前 TCR
        tcr_el1 = READ_SYSREG(TCR_EL1);

        // 设置受限的 T0SZ
        tcr_el1 &= ~TCR_T0SZ_MASK;
        tcr_el1 |= TCR_T0SZ(md->t0sz);

        // 设置其他字段
        tcr_el1 |= TCR_TG0_4K;        // 4KB 页大小
        tcr_el1 |= TCR_IRGN0_WBWA;    // Inner cacheability
        tcr_el1 |= TCR_ORGN0_WBWA;    // Outer cacheability
        tcr_el1 |= TCR_SH0_INNER;     // Shareability

        // 保存到 vCPU 上下文
        v->arch.ttbcr = tcr_el1;
    }

    return 0;
}
```

**步骤 4: 设置页表**

```c
// xen/arch/arm/monitor.c

int monitor_setup_page_tables(struct monitor_domain *md)
{
    struct domain *d = md->domain;
    lpae_t *root_table;
    unsigned int va_bits = md->va_bits;
    unsigned long va_start = 0;
    unsigned long va_end = (1UL << va_bits);

    // 1. 分配根页表
    root_table = alloc_xenheap_pages(0, MEMF_bits(d->arch.p2m.host_bits));
    if ( !root_table )
        return -ENOMEM;

    // 2. 映射 Monitor Domain 的代码和数据
    // 只映射必要的地址范围
    if ( map_pages_to_xen(va_start, mfn_x(d->arch.p2m.root_mfn),
                          va_end >> PAGE_SHIFT,
                          LPAE_SHARED | LPAE_VALID) )
    {
        free_xenheap_pages(root_table, 0);
        return -ENOMEM;
    }

    // 3. 设置 TTBR
    // 在 vCPU 切换时设置
    d->arch.monitor_ttbr = virt_to_maddr(root_table);

    return 0;
}
```

#### 13.4.3 实现监控能力

**步骤 5: 监控其他 Domain**

```c
// xen/arch/arm/monitor.c

int monitor_enable_monitoring(struct monitor_domain *md, domid_t domid)
{
    struct domain *d;

    if ( !md || !md->monitoring.enabled )
        return -EINVAL;

    d = rcu_lock_domain_by_id(domid);
    if ( !d )
        return -ENOENT;

    // 标记 Domain 为被监控
    set_bit(domid, &md->monitoring.monitored_domains);

    // 设置监控钩子
    d->arch.monitored = true;
    d->arch.monitor_domain = md->domain;

    rcu_unlock_domain(d);

    return 0;
}

// 监控事件处理
void monitor_handle_event(struct domain *d, enum monitor_event_type type,
                         void *data)
{
    struct monitor_domain *md = monitor_domain;
    struct monitor_event *event;

    if ( !md || !md->monitoring.enabled )
        return;

    if ( !test_bit(d->domain_id, &md->monitoring.monitored_domains) )
        return;

    // 记录事件
    event = xzalloc(struct monitor_event);
    if ( event )
    {
        event->type = type;
        event->domain_id = d->domain_id;
        event->timestamp = NOW();
        event->data = data;

        list_add_tail(&event->list, &md->monitoring.event_list);
    }
}
```

**步骤 6: 拦截关键操作**

```c
// xen/arch/arm/monitor.c

// 拦截内存访问
int monitor_check_memory_access(struct domain *d, paddr_t paddr,
                                unsigned int flags)
{
    struct monitor_domain *md = monitor_domain;

    if ( !md || !md->monitoring.enabled )
        return 0;

    if ( !test_bit(d->domain_id, &md->monitoring.monitored_domains) )
        return 0;

    // 检查访问权限
    if ( md->policy.restrict_memory_access )
    {
        // 实现访问控制策略
        if ( !monitor_check_access_policy(d, paddr, flags) )
        {
            monitor_handle_event(d, MONITOR_EVENT_MEMORY_VIOLATION,
                               &paddr);
            return -EPERM;
        }
    }

    return 0;
}

// 拦截 Hypercall
int monitor_check_hypercall(struct vcpu *v, unsigned int op)
{
    struct monitor_domain *md = monitor_domain;
    struct domain *d = v->domain;

    if ( !md || !md->monitoring.enabled )
        return 0;

    if ( !test_bit(d->domain_id, &md->monitoring.monitored_domains) )
        return 0;

    // 检查 Hypercall 权限
    if ( md->policy.restrict_hypercall )
    {
        if ( !monitor_check_hypercall_policy(d, op) )
        {
            monitor_handle_event(d, MONITOR_EVENT_HYPERCALL_VIOLATION,
                               &op);
            return -EPERM;
        }
    }

    return 0;
}
```

#### 13.4.4 上下文切换支持

**步骤 7: 在上下文切换时设置 Monitor Domain 的 TCR**

```c
// xen/arch/arm/domain.c (修改 context_switch)

void context_switch(struct vcpu *prev, struct vcpu *next)
{
    // ... 现有代码 ...

    // 如果是 Monitor Domain，设置受限的 TCR
    if ( is_monitor_domain(next->domain) )
    {
        struct monitor_domain *md = next->domain->arch.monitor;
        register_t tcr_el1;

        tcr_el1 = READ_SYSREG(TCR_EL1);
        tcr_el1 &= ~TCR_T0SZ_MASK;
        tcr_el1 |= TCR_T0SZ(md->t0sz);
        WRITE_SYSREG(tcr_el1, TCR_EL1);

        // 设置受限的 TTBR
        if ( md->domain->arch.monitor_ttbr )
            WRITE_SYSREG(md->domain->arch.monitor_ttbr, TTBR0_EL1);
    }

    // ... 现有代码 ...
}
```

### 13.5 安全考虑

#### 13.5.1 地址空间限制

**优势**:
- **减少攻击面**: 较小的地址空间减少可攻击的范围
- **内存保护**: 限制 Monitor Domain 只能访问必要的内存区域
- **隔离性**: 通过地址空间隔离增强安全性

**实现**:
```c
// 使用较大的 T0SZ 值限制地址空间
// T0SZ = 32 → VA_bits = 32 (4GB)
// T0SZ = 40 → VA_bits = 24 (16MB)
// T0SZ = 48 → VA_bits = 16 (64KB)

static unsigned int monitor_t0sz_values[] = {
    32,  // 4GB 地址空间
    40,  // 16MB 地址空间（更安全）
    48,  // 64KB 地址空间（最安全，但可能不够用）
};
```

#### 13.5.2 访问控制

**策略**:
- **内存访问**: 限制 Monitor Domain 只能访问授权的内存区域
- **Hypercall**: 限制可调用的 Hypercall
- **中断**: 控制中断路由和优先级

#### 13.5.3 可信启动

**要求**:
- Monitor Domain 必须从可信源加载
- 验证 Monitor Domain 的签名
- 确保 Monitor Domain 代码完整性

### 13.6 使用示例

#### 13.6.1 初始化 Monitor Domain

```c
// 在 Xen 启动时初始化
void __init monitor_init(void)
{
    int rc;

    // 创建 Monitor Domain
    rc = monitor_domain_create();
    if ( rc )
    {
        printk("Failed to create monitor domain: %d\n", rc);
        return;
    }

    // 设置地址空间
    rc = monitor_setup_address_space(monitor_domain);
    if ( rc )
    {
        printk("Failed to setup monitor address space: %d\n", rc);
        return;
    }

    // 设置页表
    rc = monitor_setup_page_tables(monitor_domain);
    if ( rc )
    {
        printk("Failed to setup monitor page tables: %d\n", rc);
        return;
    }

    // 启用监控
    monitor_domain->monitoring.enabled = true;

    printk("Monitor domain created successfully\n");
}
```

#### 13.6.2 监控 Domain

```c
// 监控特定 Domain
int monitor_domain(domid_t domid)
{
    return monitor_enable_monitoring(monitor_domain, domid);
}

// 检查内存访问
int check_memory_access(struct domain *d, paddr_t paddr, unsigned int flags)
{
    return monitor_check_memory_access(d, paddr, flags);
}
```

### 13.7 挑战和限制

#### 13.7.1 性能开销

- **地址空间限制**: 较小的地址空间可能影响性能
- **监控开销**: 监控事件处理可能引入延迟
- **页表遍历**: 受限的地址空间可能影响 TLB 效率

#### 13.7.2 兼容性

- **现有代码**: 需要修改 Xen 的上下文切换和内存管理代码
- **Guest OS**: Guest OS 需要支持受限的地址空间
- **硬件支持**: 需要硬件支持不同的 TxSZ 配置

#### 13.7.3 安全性

- **TCB 大小**: Monitor Domain 增加了可信计算基的大小
- **攻击面**: Monitor Domain 本身可能成为攻击目标
- **权限提升**: 需要防止 Monitor Domain 被滥用

### 13.8 总结

**实现要点**:
1. **创建 Monitor Domain**: 使用特殊的 Domain 标志和配置
2. **配置受限地址空间**: 通过 TxSZ 限制地址空间大小
3. **实现监控能力**: 监控其他 Domain 的行为
4. **安全隔离**: 通过地址空间和访问控制实现隔离

**优势**:
- **安全性**: 受限的地址空间减少攻击面
- **监控能力**: 可以监控和审计其他 Domain
- **隔离性**: 独立的地址空间和页表

**应用场景**:
- **安全审计**: 监控和记录系统行为
- **访问控制**: 实施细粒度的访问控制策略
- **安全研究**: 研究和分析虚拟化安全

## 十四、ARM 硬件特性相关的源码架构

### 14.1 概述

Xen Hypervisor 的 ARM 架构代码采用分层设计，通过硬件抽象层（HAL）和平台抽象层来支持不同的 ARM 硬件平台和特性。本文档介绍与 ARM 硬件特性相关的源码架构。

### 14.2 目录结构

#### 14.2.1 主要目录

```
xen/arch/arm/
├── arm32/              # ARM 32位架构特定代码
│   ├── head.S          # 启动代码
│   ├── entry.S         # 异常入口
│   ├── domain.c        # Domain 管理
│   ├── traps.c         # 异常处理
│   └── mmu/            # MMU 相关
├── arm64/              # ARM 64位架构特定代码
│   ├── head.S          # 启动代码
│   ├── entry.S         # 异常入口
│   ├── domain.c        # Domain 管理
│   ├── traps.c         # 异常处理
│   ├── cpufeature.c    # CPU 特性检测
│   └── mmu/            # MMU 相关
├── include/asm/        # ARM 架构头文件
│   ├── gic.h           # GIC 中断控制器
│   ├── cpufeature.h    # CPU 特性
│   ├── platform.h      # 平台抽象
│   ├── mm.h            # 内存管理
│   ├── processor.h     # 处理器相关
│   └── arm32/          # ARM32 特定头文件
│   └── arm64/          # ARM64 特定头文件
├── gic*.c              # GIC 中断控制器实现
├── platforms/          # 平台特定代码
│   ├── vexpress.c      # Versatile Express
│   ├── exynos5.c       # Samsung Exynos 5
│   ├── omap5.c         # TI OMAP 5
│   └── ...
├── mmu/                # 内存管理单元
│   ├── p2m.c           # 物理到机器地址转换
│   ├── pt.c            # 页表管理
│   └── setup.c         # MMU 初始化
├── vgic/               # 虚拟 GIC
├── tee/                # Trusted Execution Environment
└── ...
```

#### 14.2.2 架构分离

**ARM32 vs ARM64**:
- **共同代码**: 大部分代码在 `arch/arm/` 下，通过 `CONFIG_ARM_32` 和 `CONFIG_ARM_64` 条件编译
- **特定代码**: 架构特定的代码在 `arm32/` 和 `arm64/` 子目录
- **头文件**: 通过 `include/asm/arm32/` 和 `include/asm/arm64/` 分离

### 14.3 硬件抽象层架构

#### 14.3.1 分层设计

```
┌─────────────────────────────────────────┐
│  通用层 (Common Layer)                   │
│  - Domain 管理                          │
│  - 调度器接口                           │
│  - Hypercall 处理                       │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  ARM 架构抽象层 (ARM Architecture HAL)  │
│  - CPU 特性检测                         │
│  - 平台抽象                             │
│  - 设备树/ACPI 处理                     │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  硬件特性层 (Hardware Features)          │
│  ├── GIC (中断控制器)                   │
│  ├── MMU (内存管理单元)                 │
│  ├── SMMU (IOMMU)                      │
│  ├── PSCI (电源管理)                    │
│  └── TEE (可信执行环境)                 │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│  平台特定层 (Platform Specific)         │
│  - vexpress, exynos5, omap5, etc.      │
└─────────────────────────────────────────┘
```

### 14.4 CPU 特性检测架构

#### 14.4.1 CPU 特性抽象

**位置**: `xen/arch/arm/include/asm/cpufeature.h`

```1:100:xen/arch/arm/include/asm/cpufeature.h
#ifndef __ASM_ARM_CPUFEATURE_H
#define __ASM_ARM_CPUFEATURE_H

#ifdef CONFIG_ARM_64
#define cpu_feature64(c, feat)         ((c)->pfr64.feat)
#define boot_cpu_feature64(feat)       (system_cpuinfo.pfr64.feat)
#define boot_dbg_feature64(feat)       (system_cpuinfo.dbg64.feat)

#define cpu_feature64_has_el0_32(c)    (cpu_feature64(c, el0) == 2)

#define cpu_has_el0_32    (boot_cpu_feature64(el0) == 2)
#define cpu_has_el0_64    (boot_cpu_feature64(el0) >= 1)
#define cpu_has_el1_32    (boot_cpu_feature64(el1) == 2)
#define cpu_has_el1_64    (boot_cpu_feature64(el1) >= 1)
#define cpu_has_el2_32    (boot_cpu_feature64(el2) == 2)
#define cpu_has_el2_64    (boot_cpu_feature64(el2) >= 1)
#define cpu_has_el3_32    (boot_cpu_feature64(el3) == 2)
#define cpu_has_el3_64    (boot_cpu_feature64(el3) >= 1)
#define cpu_has_fp        (boot_cpu_feature64(fp) < 8)
#define cpu_has_simd      (boot_cpu_feature64(simd) < 8)
#define cpu_has_gicv3     (boot_cpu_feature64(gic) >= 1)
#endif

#define cpu_feature32(c, feat)         ((c)->pfr32.feat)
#define boot_cpu_feature32(feat)       (system_cpuinfo.pfr32.feat)
#define boot_dbg_feature32(feat)       (system_cpuinfo.dbg32.feat)

#define cpu_has_arm       (boot_cpu_feature32(arm) == 1)
#define cpu_has_thumb     (boot_cpu_feature32(thumb) >= 1)
#define cpu_has_thumb2    (boot_cpu_feature32(thumb) >= 3)
#define cpu_has_jazelle   (boot_cpu_feature32(jazelle) > 0)
#define cpu_has_thumbee   (boot_cpu_feature32(thumbee) == 1)
#define cpu_has_aarch32   (cpu_has_arm || cpu_has_thumb)

#ifdef CONFIG_ARM64_SVE
#define cpu_has_sve       (boot_cpu_feature64(sve) == 1)
#else
#define cpu_has_sve       0
#endif

#ifdef CONFIG_ARM_32
#define cpu_has_gicv3     (boot_cpu_feature32(gic) >= 1)
#define cpu_has_gentimer  (boot_cpu_feature32(gentimer) == 1)
#define cpu_has_pmu       ((boot_dbg_feature32(perfmon) >= 1) && \
                           (boot_dbg_feature32(perfmon) < 15))
#else
#define cpu_has_gentimer  (1)
#define cpu_has_pmu       ((boot_dbg_feature64(pmu_ver) >= 1) && \
                           (boot_dbg_feature64(pmu_ver) < 15))
#endif
#define cpu_has_security  (boot_cpu_feature32(security) > 0)

#define ARM64_WORKAROUND_CLEAN_CACHE    0
#define ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE    1
#define ARM32_WORKAROUND_766422 2
#define ARM64_WORKAROUND_834220 3
#define LIVEPATCH_FEATURE   4
#define SKIP_SYNCHRONIZE_SERROR_ENTRY_EXIT 5
#define ARM_HARDEN_BRANCH_PREDICTOR 6
#define ARM_SSBD 7
#define ARM_SMCCC_1_1 8
#define ARM64_WORKAROUND_AT_SPECULATE 9
#define ARM_WORKAROUND_858921 10
#define ARM64_WORKAROUND_REPEAT_TLBI 11
#define ARM_WORKAROUND_BHB_LOOP_8 12
#define ARM_WORKAROUND_BHB_LOOP_24 13
#define ARM_WORKAROUND_BHB_LOOP_32 14
#define ARM_WORKAROUND_BHB_SMCC_3 15
#define ARM_HAS_SB 16
#define ARM64_WORKAROUND_1508412 17

#define ARM_NCAPS           18

#ifndef __ASSEMBLY__

#include <xen/types.h>
#include <xen/lib.h>
#include <xen/bitops.h>

extern DECLARE_BITMAP(cpu_hwcaps, ARM_NCAPS);

void check_local_cpu_features(void);
void enable_cpu_features(void);

static inline bool cpus_have_cap(unsigned int num)
{
    if ( num >= ARM_NCAPS )
        return false;

    return test_bit(num, cpu_hwcaps);
}
```

**关键特性**:
- **异常级别支持**: EL0/EL1/EL2/EL3 的 32/64 位支持检测
- **指令集支持**: ARM、Thumb、Jazelle、ThumbEE
- **硬件特性**: FP、SIMD、GICv3、PMU、SVE
- **安全特性**: Security 扩展、工作区（Workaround）

#### 14.4.2 CPU 特性检测流程

**位置**: `xen/arch/arm/cpufeature.c`

```26:89:xen/arch/arm/cpufeature.c
static const struct arm_cpu_capabilities arm_features[] = {
#ifdef CONFIG_ARM_64
    {
        .desc = "Speculation barrier instruction (SB)",
        .capability = ARM_HAS_SB,
        .matches = has_sb_instruction,
    },
#endif
    {},
};

void update_cpu_capabilities(const struct arm_cpu_capabilities *caps,
                             const char *info)
{
    int i;

    for ( i = 0; caps[i].matches; i++ )
    {
        if ( !caps[i].matches(&caps[i]) )
            continue;

        if ( !cpus_have_cap(caps[i].capability) && caps[i].desc )
            printk(XENLOG_INFO "%s: %s\n", info, caps[i].desc);
        cpus_set_cap(caps[i].capability);
    }
}

void __init enable_cpu_capabilities(const struct arm_cpu_capabilities *caps)
{
    for ( ; caps->matches; caps++ )
    {
        if ( !cpus_have_cap(caps->capability) )
            continue;

        if ( caps->enable )
        {
            int ret;

            ret = stop_machine_run(caps->enable, (void *)caps, NR_CPUS);
            BUG_ON(ret);
        }
    }
}

void check_local_cpu_features(void)
{
    update_cpu_capabilities(arm_features, "enabled support for");
}

void __init enable_cpu_features(void)
{
    enable_cpu_capabilities(arm_features);
}
```

**检测流程**:
1. **定义特性表**: `arm_features[]` 数组定义所有可检测的特性
2. **匹配检测**: `matches` 函数检测特性是否存在
3. **设置标志**: 使用位图 `cpu_hwcaps` 记录检测到的特性
4. **启用特性**: `enable` 函数启用检测到的特性

### 14.5 GIC (Generic Interrupt Controller) 架构

#### 14.5.1 GIC 抽象层

**位置**: `xen/arch/arm/include/asm/gic.h`

GIC 支持多个版本：
- **GICv2**: 传统版本，使用内存映射寄存器
- **GICv3**: 支持系统寄存器接口，支持 ITS (Interrupt Translation Service)

**GIC 操作抽象**:

```c
// xen/arch/arm/include/asm/gic.h

struct gic_hw_operations {
    void (*init)(void);
    void (*secondary_init)(void);
    void (*disable_interface)(void);
    void (*send_SGI)(enum gic_sgi sgi, enum gic_sgi_mode mode,
                     const cpumask_t *cpumask);
    // ... 更多操作
};

extern const struct gic_hw_operations *gic_hw_ops;
```

**实现文件**:
- `gic-v2.c`: GICv2 实现
- `gic-v3.c`: GICv3 实现
- `gic-v3-its.c`: GICv3 ITS 扩展
- `gic.c`: GIC 通用接口和初始化

#### 14.5.2 GIC 初始化流程

**位置**: `xen/arch/arm/gic.c`

```275:290:xen/arch/arm/gic.c
void __init gic_preinit(void)
{
    if ( acpi_disabled )
        gic_dt_preinit();
    else
        gic_acpi_preinit();
}

/* Set up the GIC */
void __init gic_init(void)
{
    if ( gic_hw_ops->init() )
        panic("Failed to initialize the GIC drivers\n");
    /* Clear LR mask for cpu0 */
    clear_cpu_lr_mask();
}
```

**初始化步骤**:
1. **预初始化**: 从 Device Tree 或 ACPI 检测 GIC 版本
2. **选择实现**: 根据检测结果选择 GICv2 或 GICv3 实现
3. **初始化**: 调用 `gic_hw_ops->init()` 初始化硬件
4. **清理**: 清除 CPU 的 LR (List Register) 掩码

### 14.6 MMU (Memory Management Unit) 架构

#### 14.6.1 MMU 分层结构

```
MMU 架构
├── Stage 1 Translation (Guest 虚拟地址 → Guest 物理地址)
│   └── Guest OS 页表 (EL1/EL0)
├── Stage 2 Translation (Guest 物理地址 → 物理地址)
│   └── P2M (Physical-to-Machine) 页表 (EL2)
└── Hypervisor 页表 (Xen 虚拟地址 → 物理地址)
    └── Xen 页表 (EL2)
```

**关键文件**:
- `mmu/p2m.c`: P2M 页表管理（Stage-2 转换）
- `mmu/pt.c`: 页表操作
- `mmu/setup.c`: MMU 初始化

#### 14.6.2 P2M (Physical-to-Machine) 架构

**位置**: `xen/arch/arm/mmu/p2m.c`

P2M 负责 Guest 物理地址到机器物理地址的转换：

```c
// P2M 操作抽象
struct p2m_domain {
    // 页表根
    lpae_t *root;
    // 页表级别
    unsigned int root_order;
    unsigned int root_level;
    // IPA 地址位数
    unsigned int ipa_bits;
    // ...
};
```

**关键操作**:
- `p2m_alloc_table()`: 分配 P2M 页表
- `p2m_set_entry()`: 设置 P2M 映射
- `p2m_lookup()`: 查找 P2M 映射
- `p2m_teardown()`: 销毁 P2M 页表

### 14.7 平台抽象架构

#### 14.7.1 平台描述符

**位置**: `xen/arch/arm/include/asm/platform.h`

```9:43:xen/arch/arm/include/asm/platform.h
struct platform_desc {
    /* Platform name */
    const char *name;
    /* Array of device tree 'compatible' strings */
    const char *const *compatible;
    /* Platform initialization */
    int (*init)(void);
    int (*init_time)(void);
#ifdef CONFIG_ARM_32
    /* SMP */
    int (*smp_init)(void);
    int (*cpu_up)(int cpu);
#endif
    /* Specific mapping for dom0 */
    int (*specific_mapping)(struct domain *d);
    /* Platform reset */
    void (*reset)(void);
    /* Platform power-off */
    void (*poweroff)(void);
    /* Platform specific SMC handler */
    bool (*smc)(struct cpu_user_regs *regs);
    /*
     * Platform quirks
     * Defined has a function because a platform can support multiple
     * board with different quirk on each
     */
    uint32_t (*quirks)(void);
    /*
     * Platform blacklist devices
     * List of devices which must not pass-through to a guest
     */
    const struct dt_device_match *blacklist_dev;
    /* Override the DMA width (32-bit by default). */
    unsigned int dma_bitsize;
};
```

**平台注册宏**:

```65:71:xen/arch/arm/include/asm/platform.h
#define PLATFORM_START(_name, _namestr)                         \
static const struct platform_desc  __plat_desc_##_name __used   \
__section(".arch.info") = {                                     \
    .name = _namestr,

#define PLATFORM_END                                            \
};
```

#### 14.7.2 支持的平台

**位置**: `xen/arch/arm/platforms/`

- **vexpress.c**: ARM Versatile Express
- **exynos5.c**: Samsung Exynos 5
- **omap5.c**: TI OMAP 5
- **thunderx.c**: Cavium ThunderX
- **xilinx-zynqmp.c**: Xilinx Zynq UltraScale+
- **brcm-raspberry-pi.c**: Raspberry Pi
- **seattle.c**: AMD Seattle
- 等等...

**平台匹配**:
- 通过 Device Tree 的 `compatible` 字符串匹配
- 支持多个 `compatible` 字符串（按优先级）

### 14.8 虚拟化硬件支持

#### 14.8.1 虚拟 GIC (vGIC)

**位置**: `xen/arch/arm/vgic/`

vGIC 为 Guest 提供虚拟中断控制器：

```
vGIC 架构
├── vgic-v2.c          # GICv2 虚拟化
├── vgic-v3.c          # GICv3 虚拟化
├── vgic-v3-its.c      # GICv3 ITS 虚拟化
└── vgic/              # 通用 vGIC 代码
    ├── vgic-init.c    # vGIC 初始化
    ├── vgic-mmio.c    # MMIO 访问处理
    └── vgic.c         # vGIC 核心逻辑
```

**关键功能**:
- **中断注入**: 将物理中断注入到 Guest
- **中断虚拟化**: Guest 看到虚拟中断控制器
- **LR 管理**: 管理 List Register（GICv2）或虚拟 List Register（GICv3）

#### 14.8.2 虚拟定时器 (vTimer)

**位置**: `xen/arch/arm/vtimer.c`

ARM 架构提供虚拟定时器支持：
- **CNTV_CTL_EL0**: 虚拟定时器控制寄存器
- **CNTV_CVAL_EL0**: 虚拟定时器比较值
- **CNTVOFF_EL2**: 虚拟定时器偏移

#### 14.8.3 虚拟系统寄存器

**位置**: `xen/arch/arm/vcpreg.c`

虚拟化 ARM 系统寄存器：
- **TCR_EL1**: 转换控制寄存器
- **SCTLR_EL1**: 系统控制寄存器
- **TTBR0_EL1/TTBR1_EL1**: 页表基址寄存器
- **CPACR_EL1**: 协处理器访问控制寄存器

### 14.9 设备发现和初始化

#### 14.9.1 Device Tree 支持

**位置**: `xen/arch/arm/bootfdt.c`

Device Tree 是 ARM 架构的主要设备发现机制：

```c
// Device Tree 解析
void dt_unflatten_host_device_tree(void);
struct dt_device_node *dt_find_compatible_node(...);
int dt_device_get_address(...);
```

**支持的功能**:
- 解析扁平化设备树 (FDT)
- 查找兼容设备
- 获取设备地址和中断信息
- 设备树遍历

#### 14.9.2 ACPI 支持

**位置**: `xen/arch/arm/acpi/`

现代 ARM 系统也支持 ACPI：
- `boot.c`: ACPI 表解析
- `domain_build.c`: 基于 ACPI 的 Domain 构建
- `lib.c`: ACPI 工具函数

### 14.10 安全特性支持

#### 14.10.1 Trusted Execution Environment (TEE)

**位置**: `xen/arch/arm/tee/`

支持多种 TEE 实现：
- **OP-TEE**: `optee.c`
- **FFA (Firmware Framework for ARM)**: `ffa.c`
- **通用 TEE 接口**: `tee.c`

**功能**:
- 安全世界和非安全世界切换
- TEE 服务调用
- 安全内存管理

#### 14.10.2 安全监控调用 (SMC)

**位置**: `xen/arch/arm/vsmc.c`

处理 Guest 的 SMC (Secure Monitor Call) 指令：
- 路由到平台特定的 SMC 处理器
- 支持 PSCI (Power State Coordination Interface)
- 支持平台特定的 SMC 调用

### 14.11 架构特定的代码组织

#### 14.11.1 ARM32 vs ARM64 分离

**共同代码**:
- 大部分代码在 `arch/arm/` 下
- 通过 `CONFIG_ARM_32` 和 `CONFIG_ARM_64` 条件编译

**特定代码**:
- **ARM32**: `arch/arm/arm32/`
- **ARM64**: `arch/arm/arm64/`

**头文件分离**:
- **ARM32**: `include/asm/arm32/`
- **ARM64**: `include/asm/arm64/`

#### 14.11.2 系统寄存器访问

**位置**: `xen/arch/arm/include/asm/sysregs.h`

提供系统寄存器访问宏：
```c
#define READ_SYSREG(name)        // 读取系统寄存器
#define WRITE_SYSREG(v, name)    // 写入系统寄存器
```

**支持的寄存器**:
- **TCR_EL1/EL2**: 转换控制寄存器
- **SCTLR_EL1/EL2**: 系统控制寄存器
- **HCR_EL2**: Hypervisor 配置寄存器
- **VTCR_EL2**: 虚拟化转换控制寄存器
- 等等...

### 14.12 总结

**架构特点**:
1. **分层设计**: 通用层 → 架构抽象层 → 硬件特性层 → 平台特定层
2. **硬件抽象**: 通过操作结构体抽象不同硬件实现
3. **平台支持**: 通过平台描述符支持多种硬件平台
4. **特性检测**: 运行时检测 CPU 和硬件特性
5. **虚拟化支持**: 完整的虚拟化硬件支持（vGIC、vTimer 等）

**关键模块**:
- **CPU 特性**: `cpufeature.c`, `cpufeature.h`
- **GIC**: `gic*.c`, `vgic/`
- **MMU**: `mmu/`
- **平台**: `platforms/`
- **设备发现**: `bootfdt.c`, `acpi/`

**设计原则**:
- **可扩展性**: 易于添加新的硬件平台和特性
- **可维护性**: 清晰的抽象层和模块化设计
- **性能**: 最小化抽象层开销
- **兼容性**: 支持多种 ARM 架构版本和平台

## 十五、ARM TxSZ 相关配置位置

### 15.1 概述

TxSZ (Translation Size) 在 Xen 中有多个配置位置，分别用于不同的目的：
- **Hypervisor 自身**: 配置 Xen 的地址空间
- **Stage-2 转换**: 配置 Guest 物理地址到物理地址的转换
- **Guest Stage-1**: 配置 Guest 的虚拟地址空间

### 15.2 宏定义位置

#### 15.2.1 TCR_T0SZ 和 VTCR_T0SZ 宏

**位置**: `xen/arch/arm/include/asm/processor.h:268-359`

```268:359:xen/arch/arm/include/asm/processor.h
/* TCR: Stage 1 Translation Control */

#define TCR_T0SZ_SHIFT  (0)
#define TCR_T1SZ_SHIFT  (16)
#define TCR_T0SZ(x)     ((x)<<TCR_T0SZ_SHIFT)

/*
 * According to ARM DDI 0487B.a, TCR_EL1.{T0SZ,T1SZ} (AArch64, page D7-2480)
 * comprises 6 bits and TTBCR.{T0SZ,T1SZ} (AArch32, page G6-5204) comprises 3
 * bits following another 3 bits for RES0. Thus, the mask for both registers
 * should be 0x3f.
 */
#define TCR_SZ_MASK     (_AC(0x3f,UL))

// ... 其他 TCR 字段定义 ...

/* VTCR: Stage 2 Translation Control */

#define VTCR_T0SZ(x)    ((x)<<0)

#define VTCR_SL0(x)     ((x)<<6)
```

**关键宏**:
- **`TCR_T0SZ(x)`**: 设置 TCR_EL1 的 T0SZ 字段（位 0-5）
- **`VTCR_T0SZ(x)`**: 设置 VTCR_EL2 的 T0SZ 字段（位 0-5）
- **`TCR_SZ_MASK`**: T0SZ 字段的掩码（0x3f，6 位）

### 15.3 Hypervisor 启动时配置

#### 15.3.1 Xen 自身 TCR_EL2 配置

**位置**: `xen/arch/arm/arm64/head.S:398-416`

```398:416:xen/xen/arch/arm/arm64/head.S
        /*
         * Set up TCR_EL2:
         * Top byte is used
         * PT walks use Inner-Shareable accesses,
         * PT walks are write-back, write-allocate in both cache levels,
         * 48-bit virtual address space goes through this table.
         */
        ldr   x0, =(TCR_RES1|TCR_SH0_IS|TCR_ORGN0_WBWA|TCR_IRGN0_WBWA|TCR_T0SZ(64-48))
        /* ID_AA64MMFR0_EL1[3:0] (PARange) corresponds to TCR_EL2[18:16] (PS) */
        mrs   x1, ID_AA64MMFR0_EL1
        /* Limit to 48 bits, 256TB PA range (#5) */
        ubfm  x1, x1, #0, #3
        mov   x2, #5
        cmp   x1, x2
        csel  x1, x1, x2, lt

        bfi   x0, x1, #16, #3

        msr   tcr_el2, x0
```

**配置说明**:
- **T0SZ**: `TCR_T0SZ(64-48)` = `TCR_T0SZ(16)`，表示 48 位虚拟地址空间
- **用途**: 配置 Xen Hypervisor 自身的地址空间（Stage-1）
- **时机**: 在启动早期，MMU 启用之前

**计算**:
- `VA_bits = 64 - T0SZ = 64 - 16 = 48`
- Xen 使用 48 位虚拟地址空间

### 15.4 Stage-2 转换配置（P2M）

#### 15.4.1 VTCR_EL2 配置

**位置**: `xen/arch/arm/mmu/p2m.c:1661-1768`

```1661:1768:xen/xen/arch/arm/mmu/p2m.c
void __init setup_virt_paging(void)
{
    /* Setup Stage 2 address translation */
    register_t val = VTCR_RES1|VTCR_SH0_IS|VTCR_ORGN0_WBWA|VTCR_IRGN0_WBWA;

    static const struct {
        unsigned int pabits; /* Physical Address Size */
        unsigned int t0sz;   /* Desired T0SZ, minimum in comment */
        unsigned int root_order; /* Page order of the root of the p2m */
        unsigned int sl0;    /* Desired SL0, maximum in comment */
    } pa_range_info[] __initconst = {
        /* T0SZ minimum and SL0 maximum from ARM DDI 0487H.a Table D5-6 */
        /*      PA size, t0sz(min), root-order, sl0(max) */
#ifdef CONFIG_ARM_64
        [0] = { 32,      32/*32*/,  0,          1 },
        [1] = { 36,      28/*28*/,  0,          1 },
        [2] = { 40,      24/*24*/,  1,          1 },
        [3] = { 42,      22/*22*/,  3,          1 },
        [4] = { 44,      20/*20*/,  0,          2 },
        [5] = { 48,      16/*16*/,  0,          2 },
        [6] = { 52,      12/*12*/,  4,          2 },
        [7] = { 0 }  /* Invalid */
#else
        { 32,      0/*0*/,    0,          1 },
        { 40,      24/*24*/,  1,          1 }
#endif
    };

    // ... 选择 pa_range ...

    val |= VTCR_SL0(pa_range_info[pa_range].sl0);
    val |= VTCR_T0SZ(pa_range_info[pa_range].t0sz);

    p2m_root_order = pa_range_info[pa_range].root_order;
    p2m_root_level = 2 - pa_range_info[pa_range].sl0;

#ifdef CONFIG_ARM_64
    p2m_ipa_bits = 64 - pa_range_info[pa_range].t0sz;
#else
    t0sz_32.val = pa_range_info[pa_range].t0sz;
    p2m_ipa_bits = 32 - t0sz_32.val;
#endif

    printk("P2M: %d-bit IPA with %d-bit PA and %d-bit VMID\n",
           p2m_ipa_bits,
           pa_range_info[pa_range].pabits,
           ( MAX_VMID == MAX_VMID_16_BIT ) ? 16 : 8);

    printk("P2M: %d levels with order-%d root, VTCR 0x%"PRIregister"\n",
           4 - P2M_ROOT_LEVEL, P2M_ROOT_ORDER, val);

    // ... 写入 VTCR_EL2 ...
    vtcr = val;
    WRITE_SYSREG(vtcr, VTCR_EL2);
}
```

**配置说明**:
- **用途**: 配置 Stage-2 转换（Guest 物理地址 → 物理地址）
- **T0SZ 选择**: 根据物理地址大小（PA bits）选择对应的 T0SZ
- **IPA 计算**: `IPA_bits = 64 - T0SZ` (ARM64) 或 `IPA_bits = 32 - T0SZ` (ARM32)

**T0SZ 值表** (ARM64):

| PA bits | T0SZ | IPA bits | 说明 |
|---------|------|----------|------|
| 32      | 32   | 32       | 4GB 地址空间 |
| 36      | 28   | 36       | 64GB 地址空间 |
| 40      | 24   | 40       | 1TB 地址空间 |
| 42      | 22   | 42       | 4TB 地址空间 |
| 44      | 20   | 44       | 16TB 地址空间 |
| 48      | 16   | 48       | 256TB 地址空间 |
| 52      | 12   | 52       | 4PB 地址空间 |

### 15.5 Guest Stage-1 配置

#### 15.5.1 Guest TCR_EL1 初始化

**位置**: `xen/arch/arm/vpsci.c:45`

```45:45:xen/xen/arch/arm/vpsci.c
    ctxt->ttbcr = 0; /* Defined Reset Value */
```

**说明**:
- Guest 的 TCR_EL1 初始化为 0（复位值）
- Guest OS 会在启动时配置自己的 T0SZ

#### 15.5.2 Guest TCR_EL1 保存和恢复

**位置**: `xen/arch/arm/domain.c:86-204`

**保存 (ctxt_switch_from)**:

```129:129:xen/xen/arch/arm/domain.c
    p->arch.ttbcr = READ_SYSREG(TCR_EL1);
```

**恢复 (ctxt_switch_to)**:

```204:204:xen/xen/arch/arm/domain.c
    WRITE_SYSREG(n->arch.ttbcr, TCR_EL1);
```

**说明**:
- 在上下文切换时保存和恢复 Guest 的 TCR_EL1
- `ttbcr` 字段包含 Guest 的 T0SZ 配置
- Guest OS 可以自由配置自己的 T0SZ（在硬件限制内）

### 15.6 配置位置总结

#### 15.6.1 配置层次

```
┌─────────────────────────────────────────┐
│ 1. 宏定义                                │
│    processor.h: TCR_T0SZ, VTCR_T0SZ     │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│ 2. Hypervisor 启动配置                  │
│    arm64/head.S: TCR_EL2 (Xen 自身)     │
│    - T0SZ = 16 (48-bit VA)              │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│ 3. Stage-2 转换配置                     │
│    p2m.c: setup_virt_paging()           │
│    - VTCR_EL2.T0SZ (根据 PA bits)       │
│    - 控制 Guest IPA 空间大小            │
└─────────────────────────────────────────┘
              ↓
┌─────────────────────────────────────────┐
│ 4. Guest Stage-1 配置                   │
│    domain.c: 上下文切换                 │
│    - TCR_EL1.T0SZ (Guest 配置)          │
│    - 由 Guest OS 控制                   │
└─────────────────────────────────────────┘
```

#### 15.6.2 配置位置列表

| 配置类型 | 文件位置 | 函数/位置 | 寄存器 | 说明 |
|---------|---------|----------|--------|------|
| **宏定义** | `processor.h:270-359` | 宏定义 | - | TCR_T0SZ, VTCR_T0SZ 宏 |
| **Xen TCR_EL2** | `arm64/head.S:405` | `cpu_init()` | TCR_EL2 | Xen 自身地址空间 |
| **Stage-2 VTCR** | `p2m.c:1744` | `setup_virt_paging()` | VTCR_EL2 | Guest IPA 空间 |
| **Guest TCR_EL1** | `domain.c:129,204` | `ctxt_switch_*()` | TCR_EL1 | Guest 虚拟地址空间 |
| **Guest 初始化** | `vpsci.c:45` | `do_common_cpu_on()` | TCR_EL1 | Guest 复位值 |

#### 15.6.3 配置时机

**启动时**:
1. **汇编阶段**: `arm64/head.S` - 配置 Xen 的 TCR_EL2
2. **C 语言阶段**: `p2m.c` - 配置 Stage-2 的 VTCR_EL2

**运行时**:
1. **上下文切换**: `domain.c` - 保存/恢复 Guest 的 TCR_EL1
2. **Guest 初始化**: `vpsci.c` - 设置 Guest TCR_EL1 初始值

### 15.7 配置示例

#### 15.7.1 查看当前配置

**查看 VTCR_EL2**:
```c
// 在调试时查看
register_t vtcr = READ_SYSREG(VTCR_EL2);
unsigned int t0sz = vtcr & 0x3f;
unsigned int ipa_bits = 64 - t0sz;
printk("VTCR_EL2: 0x%lx, T0SZ: %u, IPA bits: %u\n", vtcr, t0sz, ipa_bits);
```

**查看 Guest TCR_EL1**:
```c
// 在上下文切换时查看
register_t tcr_el1 = READ_SYSREG(TCR_EL1);
unsigned int t0sz = tcr_el1 & 0x3f;
unsigned int va_bits = 64 - t0sz;
printk("TCR_EL1: 0x%lx, T0SZ: %u, VA bits: %u\n", tcr_el1, t0sz, va_bits);
```

#### 15.7.2 修改配置

**修改 Stage-2 T0SZ** (需要重新编译):
```c
// 在 p2m.c 的 pa_range_info[] 中修改
// 例如：限制为 40-bit IPA
[5] = { 48,      24/*24*/,  0,          2 },  // 改为 24，IPA = 40-bit
```

**修改 Guest T0SZ** (运行时):
```c
// Guest OS 可以通过写入 TCR_EL1 修改
// Xen 会在上下文切换时保存/恢复
```

### 15.8 关键文件索引

**宏定义**:
- `xen/arch/arm/include/asm/processor.h:270-359`
  - `TCR_T0SZ(x)`: Stage-1 T0SZ 宏
  - `VTCR_T0SZ(x)`: Stage-2 T0SZ 宏
  - `TCR_SZ_MASK`: T0SZ 字段掩码

**启动配置**:
- `xen/arch/arm/arm64/head.S:405`
  - Xen 自身 TCR_EL2 配置

**Stage-2 配置**:
- `xen/arch/arm/mmu/p2m.c:1661-1768`
  - `setup_virt_paging()`: 配置 VTCR_EL2
  - `pa_range_info[]`: T0SZ 值表

**Guest 配置**:
- `xen/arch/arm/domain.c:86-204`
  - `ctxt_switch_from()`: 保存 TCR_EL1
  - `ctxt_switch_to()`: 恢复 TCR_EL1
- `xen/arch/arm/vpsci.c:45`
  - Guest TCR_EL1 初始化

### 15.9 总结

**TxSZ 配置的三个层次**:

1. **Hypervisor 层** (TCR_EL2):
   - 位置: `arm64/head.S`
   - 用途: Xen 自身地址空间
   - 值: T0SZ = 16 (48-bit VA)

2. **Stage-2 层** (VTCR_EL2):
   - 位置: `p2m.c:setup_virt_paging()`
   - 用途: Guest IPA 空间
   - 值: 根据 PA bits 动态选择

3. **Guest 层** (TCR_EL1):
   - 位置: `domain.c` (上下文切换)
   - 用途: Guest 虚拟地址空间
   - 值: 由 Guest OS 配置

**配置流程**:
```
启动 → 配置 TCR_EL2 → 配置 VTCR_EL2 → Guest 启动 → Guest 配置 TCR_EL1
```

## 十六、ARM Watchpoint 相关源码

### 16.1 概述

ARM Watchpoint 是硬件调试功能，用于监控内存访问。在 Xen 中，Watchpoint 寄存器被虚拟化，Guest 对调试寄存器的访问会被捕获并处理。

### 16.2 调试寄存器定义

#### 16.2.1 ARM32 协处理器寄存器定义

**位置**: `xen/arch/arm/include/asm/cpregs.h:72-88`

```72:88:xen/xen/arch/arm/include/asm/cpregs.h
/* Coprocessor 14 */

/* CP14 0: Debug Register interface */
#define DBGDIDR         p14,0,c0,c0,0   /* Debug ID Register */
#define DBGDSCRINT      p14,0,c0,c1,0   /* Debug Status and Control Internal */
#define DBGDSCREXT      p14,0,c0,c2,2   /* Debug Status and Control External */
#define DBGVCR          p14,0,c0,c7,0   /* Vector Catch */
#define DBGBVR0         p14,0,c0,c0,4   /* Breakpoint Value 0 */
#define DBGBCR0         p14,0,c0,c0,5   /* Breakpoint Control 0 */
#define DBGWVR0         p14,0,c0,c0,6   /* Watchpoint Value 0 */
#define DBGWCR0         p14,0,c0,c0,7   /* Watchpoint Control 0 */
#define DBGBVR1         p14,0,c0,c1,4   /* Breakpoint Value 1 */
#define DBGBCR1         p14,0,c0,c1,5   /* Breakpoint Control 1 */
#define DBGOSLAR        p14,0,c1,c0,4   /* OS Lock Access */
#define DBGOSLSR        p14,0,c1,c1,4   /* OS Lock Status Register */
#define DBGOSDLR        p14,0,c1,c3,4   /* OS Double Lock */
#define DBGPRCR         p14,0,c1,c4,4   /* Debug Power Control Register */
```

**关键寄存器**:
- **`DBGWVR0`**: Watchpoint Value Register 0 - 存储要监控的地址
- **`DBGWCR0`**: Watchpoint Control Register 0 - 控制 Watchpoint 的行为

#### 16.2.2 ARM64 系统寄存器定义

**位置**: `xen/arch/arm/include/asm/arm64/hsr.h:51-71`

```51:71:xen/xen/arch/arm/include/asm/arm64/hsr.h
#define HSR_SYSREG_DBGBVRn_EL1(n) HSR_SYSREG(2,0,c0,c##n,4)
#define HSR_SYSREG_DBGBCRn_EL1(n) HSR_SYSREG(2,0,c0,c##n,5)
#define HSR_SYSREG_DBGWVRn_EL1(n) HSR_SYSREG(2,0,c0,c##n,6)
#define HSR_SYSREG_DBGWCRn_EL1(n) HSR_SYSREG(2,0,c0,c##n,7)

#define HSR_SYSREG_DBG_CASES(REG) case HSR_SYSREG_##REG##n_EL1(0):  \
                                  case HSR_SYSREG_##REG##n_EL1(1):  \
                                  case HSR_SYSREG_##REG##n_EL1(2):  \
                                  case HSR_SYSREG_##REG##n_EL1(3):  \
                                  case HSR_SYSREG_##REG##n_EL1(4):  \
                                  case HSR_SYSREG_##REG##n_EL1(5):  \
                                  case HSR_SYSREG_##REG##n_EL1(6):  \
                                  case HSR_SYSREG_##REG##n_EL1(7):  \
                                  case HSR_SYSREG_##REG##n_EL1(8):  \
                                  case HSR_SYSREG_##REG##n_EL1(9):  \
                                  case HSR_SYSREG_##REG##n_EL1(10): \
                                  case HSR_SYSREG_##REG##n_EL1(11): \
                                  case HSR_SYSREG_##REG##n_EL1(12): \
                                  case HSR_SYSREG_##REG##n_EL1(13): \
                                  case HSR_SYSREG_##REG##n_EL1(14): \
                                  case HSR_SYSREG_##REG##n_EL1(15)
```

**说明**:
- ARM64 支持最多 16 个 Watchpoint（DBGWVR0-15 和 DBGWCR0-15）
- 使用系统寄存器访问，而不是协处理器寄存器

### 16.3 调试寄存器陷阱配置

#### 16.3.1 MDCR_EL2 配置

**位置**: `xen/arch/arm/traps.c:138-162`

```138:162:xen/xen/arch/arm/traps.c
void init_traps(void)
{
    /*
     * Setup Hyp vector base. Note they might get updated with the
     * branch predictor hardening.
     */
    WRITE_SYSREG((vaddr_t)hyp_traps_vector, VBAR_EL2);

    /* Trap Debug and Performance Monitor accesses */
    WRITE_SYSREG(HDCR_TDRA|HDCR_TDOSA|HDCR_TDA|HDCR_TPM|HDCR_TPMCR,
                 MDCR_EL2);

    /* Trap CP15 c15 used for implementation defined registers */
    WRITE_SYSREG(HSTR_T(15), HSTR_EL2);

    WRITE_SYSREG(get_default_cptr_flags(), CPTR_EL2);

    /*
     * Configure HCR_EL2 with the bare minimum to run Xen until a guest
     * is scheduled. {A,I,F}MO bits are set to allow EL2 receiving
     * interrupts.
     */
    WRITE_SYSREG(HCR_AMO | HCR_FMO | HCR_IMO, HCR_EL2);
    isb();
}
```

**MDCR_EL2 位定义**:

**位置**: `xen/arch/arm/include/asm/processor.h:400-406`

```400:406:xen/xen/arch/arm/include/asm/processor.h
/* HDCR Hyp. Debug Configuration Register */
#define HDCR_TDRA       (_AC(1,U)<<11)          /* Trap Debug ROM access */
#define HDCR_TDOSA      (_AC(1,U)<<10)          /* Trap Debug-OS-related register access */
#define HDCR_TDA        (_AC(1,U)<<9)           /* Trap Debug Access */
#define HDCR_TDE        (_AC(1,U)<<8)           /* Route Soft Debug exceptions from EL1/EL1 to EL2 */
#define HDCR_TPM        (_AC(1,U)<<6)           /* Trap Performance Monitors accesses */
#define HDCR_TPMCR      (_AC(1,U)<<5)           /* Trap PMCR accesses */
```

**关键位**:
- **`HDCR_TDA`**: 陷阱调试访问 - 捕获对调试寄存器的访问
- **`HDCR_TDOSA`**: 陷阱调试 OS 相关寄存器访问
- **`HDCR_TDRA`**: 陷阱调试 ROM 访问

### 16.4 ARM32 Watchpoint 处理

#### 16.4.1 CP14 调试寄存器处理

**位置**: `xen/arch/arm/vcpreg.c:480-605`

```480:605:xen/xen/arch/arm/vcpreg.c
    if ( !check_conditional_instr(regs, hsr) )
    {
        advance_pc(regs, hsr);
        return;
    }

    switch ( hsr.bits & HSR_CP32_REGS_MASK )
    {
    /*
     * MDCR_EL2.TDOSA
     *
     * ARMv7 (DDI 0406C.b): B1.14.15
     * ARMv8 (DDI 0487A.d): D1-1509 Table D1-58
     *
     * Unhandled:
     *    DBGOSLSR
     *    DBGPRCR
     */
    case HSR_CPREG32(DBGOSLAR):
        return handle_wo_wi(regs, regidx, cp32.read, hsr, 1);
    case HSR_CPREG32(DBGOSDLR):
        return handle_raz_wi(regs, regidx, cp32.read, hsr, 1);

    /*
     * MDCR_EL2.TDA
     *
     * ARMv7 (DDI 0406C.b): B1.14.15
     * ARMv8 (DDI 0487A.d): D1-1510 Table D1-59
     *
     * Unhandled:
     *    DBGDCCINT
     *    DBGDTRRXint
     *    DBGDTRTXint
     *    DBGWFAR
     *    DBGDTRTXext
     *    DBGDTRRXext,
     *    DBGBXVR<n>
     *    DBGCLAIMSET
     *    DBGCLAIMCLR
     *    DBGAUTHSTATUS
     *    DBGDEVID
     *    DBGDEVID1
     *    DBGDEVID2
     *    DBGOSECCR
     */
    case HSR_CPREG32(DBGDIDR):
    {
        uint32_t val;

        /*
         * Read-only register. Accessible by EL0 if DBGDSCRext.UDCCdis
         * is set to 0, which we emulated below.
         */
        if ( !cp32.read )
            return inject_undef_exception(regs, hsr);

        /* Implement the minimum requirements:
         *  - Number of watchpoints: 1
         *  - Number of breakpoints: 2
         *  - Version: ARMv7 v7.1
         *  - Variant and Revision bits match MDIR
         */
        val = (1 << 24) | (5 << 16);
        val |= ((current_cpu_data.midr.bits >> 20) & 0xf) |
                (current_cpu_data.midr.bits & 0xf);
        set_user_reg(regs, regidx, val);

        break;
    }

    case HSR_CPREG32(DBGDSCRINT):
        /*
         * Read-only register. Accessible by EL0 if DBGDSCRext.UDCCdis
         * is set to 0, which we emulated below.
         */
        return handle_ro_raz(regs, regidx, cp32.read, hsr, 1);

    case HSR_CPREG32(DBGDSCREXT):
        /*
         * Implement debug status and control register as RAZ/WI.
         * The OS won't use Hardware debug if MDBGen not set.
         */
        return handle_raz_wi(regs, regidx, cp32.read, hsr, 1);

    case HSR_CPREG32(DBGVCR):
    case HSR_CPREG32(DBGBVR0):
    case HSR_CPREG32(DBGBCR0):
    case HSR_CPREG32(DBGWVR0):
    case HSR_CPREG32(DBGWCR0):
    case HSR_CPREG32(DBGBVR1):
    case HSR_CPREG32(DBGBCR1):
        return handle_raz_wi(regs, regidx, cp32.read, hsr, 1);
```

**关键处理**:
- **`DBGWVR0`** 和 **`DBGWCR0`**: 使用 `handle_raz_wi()` 处理
  - **RAZ/WI**: Read-As-Zero/Write-Ignore
  - Guest 读取返回 0，写入被忽略
- **`DBGDIDR`**: 返回模拟的调试 ID 寄存器值
  - 报告支持 1 个 Watchpoint 和 2 个 Breakpoint

### 16.5 ARM64 Watchpoint 处理

#### 16.5.1 系统寄存器处理

**位置**: `xen/arch/arm/arm64/vsysreg.c:155-184`

```155:184:xen/xen/arch/arm/arm64/vsysreg.c
    /*
     * MDCR_EL2.TDA
     *
     * ARMv8 (DDI 0487A.d): D1-1510 Table D1-59
     *
     * Unhandled:
     *    MDCCINT_EL1
     *    DBGDTR_EL0
     *    DBGDTRRX_EL0
     *    DBGDTRTX_EL0
     *    OSDTRRX_EL1
     *    OSDTRTX_EL1
     *    OSECCR_EL1
     *    DBGCLAIMSET_EL1
     *    DBGCLAIMCLR_EL1
     *    DBGAUTHSTATUS_EL1
     */
    case HSR_SYSREG_MDSCR_EL1:
        return handle_raz_wi(regs, regidx, hsr.sysreg.read, hsr, 1);
    case HSR_SYSREG_MDCCSR_EL0:
        /*
         * Accessible at EL0 only if MDSCR_EL1.TDCC is set to 0. We emulate that
         * register as RAZ/WI above. So RO at both EL0 and EL1.
         */
        return handle_ro_raz(regs, regidx, hsr.sysreg.read, hsr, 0);
    HSR_SYSREG_DBG_CASES(DBGBVR):
    HSR_SYSREG_DBG_CASES(DBGBCR):
    HSR_SYSREG_DBG_CASES(DBGWVR):
    HSR_SYSREG_DBG_CASES(DBGWCR):
        return handle_raz_wi(regs, regidx, hsr.sysreg.read, hsr, 1);
```

**处理说明**:
- **`HSR_SYSREG_DBG_CASES(DBGWVR)`**: 处理所有 DBGWVRn_EL1 (n=0-15)
- **`HSR_SYSREG_DBG_CASES(DBGWCR)`**: 处理所有 DBGWCRn_EL1 (n=0-15)
- 所有 Watchpoint 寄存器都使用 `handle_raz_wi()` 处理

### 16.6 异常类型定义

#### 16.6.1 HSR 异常类编码

**位置**: `xen/arch/arm/include/asm/processor.h:408-437`

```408:437:xen/xen/arch/arm/include/asm/processor.h
#define HSR_EC_SHIFT                26

#define HSR_EC_UNKNOWN              0x00
#define HSR_EC_WFI_WFE              0x01
#define HSR_EC_CP15_32              0x03
#define HSR_EC_CP15_64              0x04
#define HSR_EC_CP14_32              0x05        /* Trapped MCR or MRC access to CP14 */
#define HSR_EC_CP14_DBG             0x06        /* Trapped LDC/STC access to CP14 (only for debug registers) */
#define HSR_EC_CP                   0x07        /* HCPTR-trapped access to CP0-CP13 */
#define HSR_EC_CP10                 0x08
#define HSR_EC_JAZELLE              0x09
#define HSR_EC_BXJ                  0x0a
#define HSR_EC_CP14_64              0x0c
#define HSR_EC_SVC32                0x11
#define HSR_EC_HVC32                0x12
#define HSR_EC_SMC32                0x13
#ifdef CONFIG_ARM_64
#define HSR_EC_SVC64                0x15
#define HSR_EC_HVC64                0x16
#define HSR_EC_SMC64                0x17
#define HSR_EC_SYSREG               0x18
#define HSR_EC_SVE                  0x19
#endif
#define HSR_EC_INSTR_ABORT_LOWER_EL 0x20
#define HSR_EC_INSTR_ABORT_CURR_EL  0x21
#define HSR_EC_DATA_ABORT_LOWER_EL  0x24
#define HSR_EC_DATA_ABORT_CURR_EL   0x25
#ifdef CONFIG_ARM_64
#define HSR_EC_BRK                  0x3c
#endif
```

**关键异常类**:
- **`HSR_EC_CP14_32`**: ARM32 协处理器 14 访问（MCR/MRC）
- **`HSR_EC_CP14_DBG`**: ARM32 调试寄存器访问（LDC/STC）
- **`HSR_EC_SYSREG`**: ARM64 系统寄存器访问

### 16.7 源码架构总结

#### 16.7.1 文件组织

```
ARM Watchpoint 源码架构
├── 寄存器定义
│   ├── cpregs.h          # ARM32 协处理器寄存器定义
│   └── arm64/hsr.h       # ARM64 系统寄存器编码
├── 陷阱配置
│   ├── traps.c           # init_traps() - 配置 MDCR_EL2
│   └── processor.h       # HDCR 位定义
└── 寄存器处理
    ├── vcpreg.c          # ARM32 CP14 处理
    └── arm64/vsysreg.c   # ARM64 系统寄存器处理
```

#### 16.7.2 处理流程

```
Guest 访问调试寄存器
    ↓
MDCR_EL2.TDA 触发陷阱
    ↓
HSR 异常类编码
    ├── ARM32: HSR_EC_CP14_32 或 HSR_EC_CP14_DBG
    └── ARM64: HSR_EC_SYSREG
    ↓
路由到处理函数
    ├── ARM32: do_cp14_32() → DBGWVR0/DBGWCR0 case
    └── ARM64: do_sysreg() → DBGWVRn_EL1/DBGWCRn_EL1 case
    ↓
handle_raz_wi() 处理
    ├── 读取: 返回 0
    └── 写入: 忽略
```

#### 16.7.3 关键代码位置

| 功能 | 文件 | 行号 | 说明 |
|------|------|------|------|
| **寄存器定义** | `cpregs.h` | 81-82 | DBGWVR0, DBGWCR0 |
| **ARM64 定义** | `arm64/hsr.h` | 53-54 | DBGWVRn_EL1, DBGWCRn_EL1 |
| **陷阱配置** | `traps.c` | 147-148 | MDCR_EL2 配置 |
| **位定义** | `processor.h` | 400-406 | HDCR 位定义 |
| **ARM32 处理** | `vcpreg.c` | 568-572 | DBGWVR0/DBGWCR0 处理 |
| **ARM64 处理** | `arm64/vsysreg.c` | 182-184 | DBGWVRn/DBGWCRn 处理 |

### 16.8 当前实现状态

#### 16.8.1 支持的功能

1. **寄存器陷阱**: 通过 MDCR_EL2.TDA 捕获 Guest 访问
2. **基本模拟**: 通过 RAZ/WI 模拟寄存器
3. **ID 寄存器**: 模拟 DBGDIDR，报告支持的 Watchpoint 数量

#### 16.8.2 未实现的功能

1. **实际 Watchpoint**: 当前实现不支持实际的硬件 Watchpoint
2. **Watchpoint 触发**: 不支持 Watchpoint 命中时的异常处理
3. **上下文切换**: 不支持 Watchpoint 状态的保存/恢复
4. **多 Watchpoint**: 虽然定义了多个寄存器，但都返回 0

#### 16.8.3 设计考虑

**为什么使用 RAZ/WI**:
- **安全性**: 防止 Guest 使用硬件调试功能
- **简化**: 不需要实现复杂的 Watchpoint 虚拟化
- **兼容性**: Guest OS 可以正常启动，但无法使用硬件调试

**未来扩展**:
- 如果需要支持 Guest 调试，需要：
  1. 实现 Watchpoint 状态的保存/恢复
  2. 在上下文切换时处理 Watchpoint 寄存器
  3. 实现 Watchpoint 命中的异常处理
  4. 支持多个 Watchpoint 的虚拟化

### 16.9 总结

**ARM Watchpoint 在 Xen 中的实现**:

1. **寄存器定义**: 支持 ARM32 和 ARM64 的 Watchpoint 寄存器
2. **陷阱配置**: 通过 MDCR_EL2.TDA 捕获 Guest 访问
3. **基本模拟**: 使用 RAZ/WI 模拟，不提供实际功能
4. **设计目标**: 安全性和兼容性，而非功能完整性

**关键特点**:
- **ARM32**: 通过协处理器 14 访问，支持 DBGWVR0/DBGWCR0
- **ARM64**: 通过系统寄存器访问，支持 DBGWVRn_EL1/DBGWCRn_EL1 (n=0-15)
- **处理方式**: 统一使用 `handle_raz_wi()` 处理
- **当前状态**: 仅提供基本模拟，不支持实际 Watchpoint 功能

## 十七、参考文档

- [ARM 架构启动流程](./arm-startup-flow.md)
- [ARM 架构特定启动流程](./arm-arch-specific-startup.md)
- [通用启动流程](./common-startup-flow.md)
- [Domain 0 的本质](./domain0-essence.md)
