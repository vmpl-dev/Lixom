# ARM 架构 Xen Hypervisor 启动流程分析（总览）

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/arch/arm/arm64/head.S` - ARM64 汇编启动代码
- `xen/xen/arch/arm/setup.c` - ARM 架构 C 语言启动代码
- `xen/xen/arch/arm/domain_build.c` - Domain 构建代码

## 概述

Xen Hypervisor 的启动流程是理解整个系统的基础。本文档是 ARM 架构启动流程的总览。

**注意**: 为了更好的理解，启动流程已分为两部分：
1. **[ARM 架构特定的启动流程](./arm-arch-specific-startup.md)** - ARM 架构独有的代码（汇编、GIC、Device Tree 等）
2. **[架构无关的通用启动流程](./common-startup-flow.md)** - 所有架构共享的代码（调度器、内存管理、Domain 创建等）

## 文档结构

本文档提供启动流程的概览和流程图。详细分析请参考上述两个文档。

ARM 架构的 Xen Hypervisor 启动流程分为两个主要阶段：
1. **汇编阶段**：底层硬件初始化，MMU 设置，跳转到 C 代码
2. **C 语言阶段**：内存管理、设备初始化、Domain 0 创建

## 一、汇编启动阶段 (head.S)

### 1.1 入口点

**位置**: `xen/xen/arch/arm/arm64/head.S:112`

```112:123:xen/xen/arch/arm/arm64/head.S
GLOBAL(start)
        /*
         * DO NOT MODIFY. Image header expected by Linux boot-loaders.
         */
efi_head:
        /*
         * This add instruction has no meaningful effect except that
         * its opcode forms the magic "MZ" signature of a PE/COFF file
         * that is required for UEFI applications.
         */
        add     x13, x18, #0x16
        b       real_start           /* branch to kernel start */
```

### 1.2 启动要求

根据代码注释，ARM 架构启动时的要求：
- **MMU**: 关闭
- **D-cache**: 关闭
- **I-cache**: 开启或关闭都可以
- **x0**: FDT (Flattened Device Tree) 物理地址
- **异常级别**: 必须在 EL2 (Hypervisor 模式)

### 1.3 关键初始化步骤

#### 步骤 1: 禁用中断并保存参数

```245:253:xen/xen/arch/arm/arm64/head.S
real_start:
        /* BSS should be zeroed when booting without EFI */
        mov   x26, #0                /* x26 := skip_zero_bss */

real_start_efi:
        msr   DAIFSet, 0xf           /* Disable all interrupts */

        /* Save the bootloader arguments in less-clobberable registers */
        mov   x21, x0                /* x21 := DTB, physical address  */
```

#### 步骤 2: 计算物理地址偏移

```255:258:xen/xen/arch/arm/arm64/head.S
        /* Find out where we are */
        ldr   x0, =start
        adr   x19, start             /* x19 := paddr (start) */
        sub   x20, x19, x0           /* x20 := phys-offset */
```

#### 步骤 3: 检查 CPU 模式

```343:359:xen/xen/arch/arm/arm64/head.S
check_cpu_mode:
        PRINT("- Current EL ")
        mrs   x5, CurrentEL
        print_reg x5
        PRINT(" -\r\n")

        /* Are we in EL2 */
        cmp   x5, #PSR_MODE_EL2t
        ccmp  x5, #PSR_MODE_EL2h, #0x4, ne
        b.ne  1f /* No */
        ret
1:
        /* OK, we're boned. */
        PRINT("- Xen must be entered in NS EL2 mode -\r\n")
        PRINT("- Please update the bootloader -\r\n")
        b fail
```

**关键点**: Xen 必须在 EL2 (Hypervisor 模式) 下运行，否则启动失败。

#### 步骤 4: CPU 初始化

```390:429:xen/xen/arch/arm/arm64/head.S
cpu_init:
        PRINT("- Initialize CPU -\r\n")

        /* Set up memory attribute type tables */
        ldr   x0, =MAIRVAL
        msr   mair_el2, x0

        /*
         * Set up TCR_EL2:
         * PS -- Based on ID_AA64MMFR0_EL1.PARange
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

        ldr   x0, =SCTLR_EL2_SET
        msr   SCTLR_EL2, x0
        isb

        /*
         * Ensure that any exceptions encountered at EL2
         * are handled using the EL2 stack pointer, rather
         * than SP_EL0.
         */
        msr spsel, #1
        ret
```

**关键操作**:
- 设置 MAIR (Memory Attribute Indirection Register)
- 配置 TCR_EL2 (Translation Control Register)
- 配置 SCTLR_EL2 (System Control Register)
- 设置使用 EL2 栈指针

#### 步骤 5: 启用 MMU 并跳转到 C 代码

```277:288:xen/xen/arch/arm/arm64/head.S
primary_switched:
#ifdef CONFIG_EARLY_PRINTK
        /* Use a virtual address to access the UART. */
        ldr   x23, =EARLY_UART_VIRTUAL_ADDRESS
#endif
        bl    zero_bss
        PRINT("- Ready -\r\n")
        /* Setup the arguments for start_xen and jump to C world */
        mov   x0, x20                /* x0 := Physical offset */
        mov   x1, x21                /* x1 := paddr(FDT) */
        ldr   x2, =start_xen
        b     launch
```

## 二、C 语言启动阶段 (setup.c)

### 2.1 主入口函数

**位置**: `xen/xen/arch/arm/setup.c:763`

```763:770:xen/xen/arch/arm/setup.c
void __init start_xen(unsigned long boot_phys_offset,
                      unsigned long fdt_paddr)
{
    size_t fdt_size;
    const char *cmdline;
    struct bootmodule *xen_bootmodule;
    struct domain *d;
    int rc, i;
```

**参数说明**:
- `boot_phys_offset`: 物理地址偏移量
- `fdt_paddr`: 设备树 (FDT) 的物理地址

### 2.2 启动流程详解

#### 阶段 1: 早期初始化 (772-803)

```772:803:xen/xen/arch/arm/setup.c
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
1. 初始化每 CPU 区域
2. 设置虚拟内存区域
3. 初始化异常处理
4. 设置页表
5. 映射设备树
6. 解析命令行参数

#### 阶段 2: 内存管理初始化 (804-815)

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
- 设置内存管理子系统
- 解析 ACPI 表（如果启用）
- 结束启动分配器
- 切换系统状态到 `SYS_STATE_boot`

#### 阶段 3: 虚拟化和设备树处理 (817-829)

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
- 初始化虚拟化子系统
- 根据配置选择 Device Tree 或 ACPI
- ARM 架构通常使用 Device Tree

#### 阶段 4: 中断和平台初始化 (831-859)

```831:859:xen/xen/arch/arm/setup.c
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
- 初始化中断系统
- 平台特定初始化
- GIC (Generic Interrupt Controller) 初始化
- 串口和控制台初始化
- CPU 特性检测
- SMP (多核) 初始化

#### 阶段 5: 调度器和系统域设置 (861-889)

```861:889:xen/xen/arch/arm/setup.c
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

    local_irq_enable();
    local_abort_enable();

    smp_prepare_cpus();

    initialize_keytable();

    console_init_postirq();

    do_presmp_initcalls();
```

**关键操作**:
- 初始化任务队列子系统
- XSM (Xen Security Module) 初始化
- 初始化维护中断和定时器中断
- 初始化空闲域
- RCU (Read-Copy-Update) 初始化
- 启用中断
- 准备多核启动

#### 阶段 6: 多核启动 (891-905)

```891:905:xen/xen/arch/arm/setup.c
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

    /* This should be done in a vpmu driver but we do not have one yet. */
    vpmu_is_available = cpu_has_pmu;
```

**关键操作**:
- 启动所有可用的 CPU 核心
- 设置虚拟性能监控单元 (vPMU)

#### 阶段 7: IOMMU 和虚拟分页 (912-918)

```912:918:xen/xen/arch/arm/setup.c
    /*
     * The IOMMU subsystem must be initialized before P2M as we need
     * to gather requirements regarding the maximum IPA bits supported by
     * each IOMMU device.
     */
    rc = iommu_setup();
    if ( !iommu_enabled && rc != -ENODEV )
        panic("Couldn't configure correctly all the IOMMUs.\n");

    setup_virt_paging();
```

**关键操作**:
- IOMMU 初始化（必须在 P2M 之前）
- 设置虚拟分页

#### 阶段 8: 初始化调用和 Domain 0 创建 (918-938)

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
- 执行所有初始化调用
- 应用 CPU 替代方案和错误修复
- **创建 Domain 0**（除非是 dom0less 模式）
- 创建其他 Domain U（如果配置了）

### 2.3 Domain 0 创建流程

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
    int rc;

    /* The vGIC for DOM0 is exactly emulating the hardware GIC */
    dom0_cfg.arch.gic_version = XEN_DOMCTL_CONFIG_GIC_NATIVE;
    /*
     * Xen vGIC supports a maximum of 992 interrupt lines.
     * 32 are substracted to cover local IRQs.
     */
    dom0_cfg.arch.nr_spis = min(gic_number_lines(), (unsigned int) 992) - 32;
    if ( gic_number_lines() > 992 )
        printk(XENLOG_WARNING "Maximum number of vGIC IRQs exceeded.\n");
    dom0_cfg.arch.tee_type = tee_get_type();
    dom0_cfg.max_vcpus = dom0_max_vcpus();

    if ( iommu_enabled )
        dom0_cfg.flags |= XEN_DOMCTL_CDF_iommu;

    if ( opt_dom0_sve )
    {
        unsigned int vl;

        if ( sve_domctl_vl_param(opt_dom0_sve, &vl) )
            dom0_cfg.arch.sve_vl = sve_encode_vl(vl);
        else
            panic("SVE vector length error\n");
    }

    dom0 = domain_create(0, &dom0_cfg, CDF_privileged | CDF_directmap);
    if ( IS_ERR(dom0) )
        panic("Error creating domain 0 (rc = %ld)\n", PTR_ERR(dom0));

    if ( alloc_dom0_vcpu0(dom0) == NULL )
        panic("Error creating domain 0 vcpu0\n");

    rc = construct_dom0(dom0);
    if ( rc )
        panic("Could not set up DOM0 guest OS (rc = %d)\n", rc);
}
```

**关键配置**:
- **flags**: `XEN_DOMCTL_CDF_hvm | XEN_DOMCTL_CDF_hap` - HVM 模式和硬件辅助分页
- **gic_version**: `XEN_DOMCTL_CONFIG_GIC_NATIVE` - 使用原生 GIC
- **特权标志**: `CDF_privileged | CDF_directmap` - 特权域，直接内存映射

#### Domain 0 构建过程

```3808:3880:xen/xen/arch/arm/domain_build.c
static int __init construct_dom0(struct domain *d)
{
    struct kernel_info kinfo = {};
    int rc;
#ifdef CONFIG_STATIC_SHM
    const struct dt_device_node *chosen = dt_find_node_by_path("/chosen");
#endif

    /* Sanity! */
    BUG_ON(d->domain_id != 0);

    printk("*** LOADING DOMAIN 0 ***\n");

    /* The ordering of operands is to work around a clang5 issue. */
    if ( CONFIG_DOM0_MEM[0] && !dom0_mem_set )
        parse_dom0_mem(CONFIG_DOM0_MEM);

    if ( dom0_mem <= 0 )
    {
        warning_add("PLEASE SPECIFY dom0_mem PARAMETER - USING 512M FOR NOW\n");
        dom0_mem = MB(512);
    }

    iommu_hwdom_init(d);

    d->max_pages = dom0_mem >> PAGE_SHIFT;

    kinfo.unassigned_mem = dom0_mem;
    kinfo.d = d;

    rc = kernel_probe(&kinfo, NULL);
    if ( rc < 0 )
        return rc;

#ifdef CONFIG_ARM_64
    /* type must be set before allocate_memory */
    d->arch.type = kinfo.type;
#endif
    allocate_memory_11(d, &kinfo);
    find_gnttab_region(d, &kinfo);

#ifdef CONFIG_STATIC_SHM
    rc = process_shm(d, &kinfo, chosen);
    if ( rc < 0 )
        return rc;
#endif

    /* Map extra GIC MMIO, irqs and other hw stuffs to dom0. */
    rc = gic_map_hwdom_extra_mappings(d);
    if ( rc < 0 )
        return rc;

    rc = platform_specific_mapping(d);
    if ( rc < 0 )
        return rc;

    if ( acpi_disabled )
    {
        rc = prepare_dtb_hwdom(d, &kinfo);
        if ( rc < 0 )
            return rc;
#ifdef CONFIG_HAS_PCI
        rc = pci_host_bridge_mappings(d);
#endif
    }
    else
        rc = prepare_acpi(d, &kinfo);

    if ( rc < 0 )
        return rc;

    return construct_domain(d, &kinfo);
}
```

**关键步骤**:
1. 解析 Domain 0 内存配置（默认 512MB）
2. 初始化 IOMMU
3. 探测内核镜像
4. 分配内存
5. 映射 GIC 和平台特定资源
6. 准备设备树或 ACPI
7. 构建 Domain

### 2.4 启动完成 (940-970)

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
}
```

**关键操作**:
- 丢弃初始模块
- 初始化堆内存
- 初始化跟踪缓冲区
- 设置系统状态为 `SYS_STATE_active`
- 取消暂停所有域
- 切换到动态分配的栈并跳转到 `init_done()`

## 三、启动流程图

```
Bootloader
    |
    v
[head.S: start]
    |
    v
[检查 EL2 模式]
    |
    v
[CPU 初始化]
    |
    v
[启用 MMU]
    |
    v
[跳转到 C 代码]
    |
    v
[setup.c: start_xen()]
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
    ├─> acpi_boot_table_init()
    └─> end_boot_allocator()
    |
    v
[虚拟化初始化]
    ├─> vm_init()
    └─> dt_unflatten_host_device_tree()
    |
    v
[中断和平台]
    ├─> init_IRQ()
    ├─> platform_init()
    ├─> gic_init()
    └─> smp_init_cpus()
    |
    v
[调度器初始化]
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
    ├─> construct_dom0()
    └─> construct_domain()
    |
    v
[系统激活]
    ├─> system_state = SYS_STATE_active
    └─> domain_unpause_by_systemcontroller()
    |
    v
[init_done()]
    └─> startup_cpu_idle_loop()
```

## 四、关键概念

### 4.1 异常级别 (Exception Levels)

ARMv8 架构定义了 4 个异常级别：
- **EL0**: 用户模式（Guest OS）
- **EL1**: 内核模式（Guest OS 内核）
- **EL2**: Hypervisor 模式（Xen 运行在此级别）
- **EL3**: Secure Monitor 模式

Xen 必须在 EL2 级别运行。

### 4.2 Device Tree vs ACPI

ARM 架构传统上使用 **Device Tree (DT)** 来描述硬件：
- 扁平化的设备树 (FDT - Flattened Device Tree)
- 包含 CPU、内存、中断控制器、设备等信息
- 在启动时由 bootloader 传递给 Xen

现代 ARM 系统也支持 **ACPI**，但 Device Tree 更常见。

### 4.3 GIC (Generic Interrupt Controller)

ARM 架构使用 GIC 处理中断：
- **GICv2**: 传统版本
- **GICv3**: 支持系统寄存器接口
- Domain 0 使用原生 GIC 配置

### 4.4 Domain 0 特性

- **CDF_privileged**: 特权域，可以管理其他域
- **CDF_directmap**: 直接内存映射，可以直接访问物理内存
- **HVM + HAP**: 硬件虚拟化模式 + 硬件辅助分页

## 五、理解总结

1. **启动顺序**: 汇编初始化 → C 语言初始化 → 内存管理 → 虚拟化 → Domain 0 创建
2. **关键要求**: 必须在 EL2 模式，MMU 和 D-cache 初始关闭
3. **设备发现**: 通过 Device Tree 或 ACPI 发现硬件
4. **Domain 0**: 第一个也是最重要的域，负责系统管理

## 六、疑问和待研究

- [ ] MMU 启用的具体时机和过程
- [ ] GIC 虚拟化的详细实现
- [ ] 多核启动时的同步机制
- [ ] Domain 0 内存分配的详细策略
- [ ] dom0less 模式的工作原理

## 七、参考链接

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [Device Tree Specification](https://www.devicetree.org/specifications/)
- [Xen ARM Documentation](https://wiki.xenproject.org/wiki/Xen_ARM_with_Virtualization_Extensions)
- `xen/xen/arch/arm/setup.c`
- `xen/xen/arch/arm/arm64/head.S`
