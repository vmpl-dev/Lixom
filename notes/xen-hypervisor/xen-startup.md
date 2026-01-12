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

## 九、参考文档

- [ARM 架构启动流程](./arm-startup-flow.md)
- [ARM 架构特定启动流程](./arm-arch-specific-startup.md)
- [通用启动流程](./common-startup-flow.md)
- [Domain 0 的本质](./domain0-essence.md)
