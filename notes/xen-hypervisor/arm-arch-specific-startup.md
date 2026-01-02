# ARM 架构特定的启动流程

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/arch/arm/arm64/head.S` - ARM64 汇编启动代码
- `xen/xen/arch/arm/setup.c` - ARM 架构 C 语言启动代码
- `xen/xen/arch/arm/include/asm/setup.h` - ARM 架构特定定义

## 概述

本文档专注于 ARM 架构特定的启动流程，包括汇编层面的初始化、架构特定的硬件抽象层（HAL）初始化等。这些代码是 ARM 架构独有的，不适用于 x86、PPC 等其他架构。

## 一、汇编启动阶段 (head.S)

### 1.1 入口点和启动要求

**位置**: `xen/xen/arch/arm/arm64/head.S:112`

ARM 架构的启动入口点是 `start` 标签，启动时的硬件状态要求：

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

**启动要求**（根据代码注释）:
- **MMU**: 关闭
- **D-cache**: 关闭
- **I-cache**: 开启或关闭都可以
- **x0**: FDT (Flattened Device Tree) 物理地址
- **异常级别**: 必须在 **EL2** (Hypervisor 模式)

### 1.2 早期初始化

#### 禁用中断并保存参数

```245:253:xen/xen/arch/arm/arm64/head.S
real_start:
        /* BSS should be zeroed when booting without EFI */
        mov   x26, #0                /* x26 := skip_zero_bss */

real_start_efi:
        msr   DAIFSet, 0xf           /* Disable all interrupts */

        /* Save the bootloader arguments in less-clobberable registers */
        mov   x21, x0                /* x21 := DTB, physical address  */
```

#### 计算物理地址偏移

```255:258:xen/xen/arch/arm/arm64/head.S
        /* Find out where we are */
        ldr   x0, =start
        adr   x19, start             /* x19 := paddr (start) */
        sub   x20, x19, x0           /* x20 := phys-offset */
```

**关键点**: ARM 架构需要计算物理地址偏移，因为代码可能被加载到任意物理地址。

### 1.3 CPU 模式检查

**位置**: `xen/xen/arch/arm/arm64/head.S:343`

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

**关键要求**:
- ARMv8 架构定义了 4 个异常级别：EL0 (用户), EL1 (内核), **EL2 (Hypervisor)**, EL3 (Secure Monitor)
- Xen 必须在 **EL2** 级别运行，否则启动失败

### 1.4 CPU 初始化

**位置**: `xen/xen/arch/arm/arm64/head.S:390`

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

**关键寄存器配置**:
1. **MAIR_EL2** (Memory Attribute Indirection Register): 定义内存属性类型
2. **TCR_EL2** (Translation Control Register):
   - 配置页表遍历属性
   - 设置物理地址范围 (PS)
   - 48 位虚拟地址空间
3. **SCTLR_EL2** (System Control Register): 系统控制设置
4. **SPsel**: 使用 EL2 栈指针

### 1.5 跳转到 C 代码

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

**参数传递**:
- `x0`: 物理地址偏移量
- `x1`: 设备树物理地址

## 二、ARM 架构特定的 C 语言初始化

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

**函数签名特点**: ARM 架构的 `start_xen()` 接收两个参数：
- `boot_phys_offset`: 物理地址偏移
- `fdt_paddr`: 设备树物理地址

这与 x86 架构的 `__start_xen(unsigned long mbi_p)` 不同。

### 2.2 架构特定的早期初始化

#### 每 CPU 区域初始化

```772:776:xen/xen/arch/arm/setup.c
    dcache_line_bytes = read_dcache_line_bytes();

    percpu_init_areas();
    set_processor_id(0); /* needed early, for smp_processor_id() */
```

**架构特定点**: `read_dcache_line_bytes()` 是 ARM 架构特定的，用于读取数据缓存行大小。

#### 异常处理初始化

```777:779:xen/xen/arch/arm/setup.c
    setup_virtual_regions(NULL, NULL);
    /* Initialize traps early allow us to get backtrace when an error occurred */
    init_traps();
```

#### 页表设置

```781:781:xen/xen/arch/arm/setup.c
    setup_pagetables(boot_phys_offset);
```

**架构特定**: ARM 架构的页表设置需要考虑物理地址偏移。

### 2.3 Device Tree 处理

ARM 架构传统上使用 **Device Tree (DT)** 来描述硬件，而不是 ACPI。

#### 映射设备树

```785:790:xen/xen/arch/arm/setup.c
    device_tree_flattened = early_fdt_map(fdt_paddr);
    if ( !device_tree_flattened )
        panic("Invalid device tree blob at physical address %#lx.\n"
              "The DTB must be 8-byte aligned and must not exceed 2 MB in size.\n\n"
              "Please check your bootloader.\n",
              fdt_paddr);
```

#### 解析设备树信息

```798:802:xen/xen/arch/arm/setup.c
    fdt_size = boot_fdt_info(device_tree_flattened, fdt_paddr);

    cmdline = boot_fdt_cmdline(device_tree_flattened);
    printk("Command line: %s\n", cmdline);
    cmdline_parse(cmdline);
```

### 2.4 架构特定的内存管理初始化

```804:804:xen/xen/arch/arm/setup.c
    setup_mm();
```

这是 ARM 架构特定的内存管理初始化，与 x86 的实现不同。

### 2.5 Device Tree vs ACPI 选择

```819:829:xen/xen/arch/arm/setup.c
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

**架构特点**: ARM 架构传统上使用 Device Tree，但现代系统也支持 ACPI。

### 2.6 中断控制器初始化 (GIC)

ARM 架构使用 **GIC (Generic Interrupt Controller)** 处理中断。

#### GIC 预初始化

```837:837:xen/xen/arch/arm/setup.c
    gic_preinit();
```

#### GIC 完整初始化

```859:859:xen/xen/arch/arm/setup.c
    gic_init();
```

**架构特定**: GIC 是 ARM 架构特有的中断控制器，x86 使用 APIC。

### 2.7 串口初始化

```839:841:xen/xen/arch/arm/setup.c
    arm_uart_init();
    console_init_preirq();
    console_init_ring();
```

**架构特定**: `arm_uart_init()` 是 ARM 架构特定的串口初始化。

### 2.8 CPU 信息打印

```843:843:xen/xen/arch/arm/setup.c
    processor_id();
```

这个函数打印 ARM 架构特定的 CPU 信息，包括：
- 处理器实现者（ARM、Broadcom、Qualcomm 等）
- 异常级别支持（EL3、EL2、EL1、EL0）
- 扩展支持（浮点、SIMD、SVE 等）

### 2.9 SMP 初始化

```845:847:xen/xen/arch/arm/setup.c
    smp_init_cpus();
    nr_cpu_ids = smp_get_max_cpus();
    printk(XENLOG_INFO "SMP: Allowing %u CPUs\n", nr_cpu_ids);
```

ARM 架构的 SMP 初始化使用 **PSCI (Power State Coordination Interface)** 协议。

### 2.10 CPU 错误修复和特性检查

```853:855:xen/xen/arch/arm/setup.c
    check_local_cpu_errata();

    check_local_cpu_features();
```

这些函数检查 ARM 架构特定的 CPU 错误和特性。

### 2.11 时间初始化

```857:857:xen/xen/arch/arm/setup.c
    init_xen_time();
```

ARM 架构使用 **Generic Timer** 作为时间源。

### 2.12 平台特定初始化

```833:833:xen/xen/arch/arm/setup.c
    platform_init();
```

ARM 架构支持平台特定的初始化，例如：
- Broadcom 平台
- Qualcomm 平台
- 通用平台

### 2.13 IOMMU 初始化

```912:914:xen/xen/arch/arm/setup.c
    rc = iommu_setup();
    if ( !iommu_enabled && rc != -ENODEV )
        panic("Couldn't configure correctly all the IOMMUs.\n");
```

ARM 架构使用 **SMMU (System Memory Management Unit)** 作为 IOMMU。

### 2.14 虚拟分页设置

```916:916:xen/xen/arch/arm/setup.c
    setup_virt_paging();
```

ARM 架构的虚拟分页设置，包括 P2M (Physical-to-Machine) 映射。

## 三、ARM 架构特定的 Domain 0 创建

**重要说明**: Domain 0 本质上就是一个普通虚拟机，只是被赋予了特权。Domain 0 的内核不在 Xen 源码中，需要单独获取。详见 [Domain 0 的本质](./domain0-essence.md)。

### 3.1 Domain 0 配置

**位置**: `xen/xen/arch/arm/domain_build.c:3882`

```3882:3904:xen/xen/arch/arm/domain_build.c
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
```

**架构特定配置**:
- **gic_version**: 使用原生 GIC 配置
- **nr_spis**: GIC 支持的中断线数量（最多 992 条）
- **tee_type**: TEE (Trusted Execution Environment) 类型

**关键点**: Domain 0 通过 `CDF_privileged` 标志（在 `domain_create()` 调用中）获得特权，但创建过程与其他 Domain 相同。

### 3.2 Domain 0 构建

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

**架构特定步骤**:
1. **GIC 映射**: `gic_map_hwdom_extra_mappings()` - 映射 GIC MMIO 到 Domain 0
2. **平台特定映射**: `platform_specific_mapping()` - 平台特定的设备映射
3. **设备树准备**: `prepare_dtb_hwdom()` - 为 Domain 0 准备设备树

## 四、ARM 架构关键概念

### 4.1 异常级别 (Exception Levels)

ARMv8 架构定义了 4 个异常级别：
- **EL0**: 用户模式（Guest OS 用户空间）
- **EL1**: 内核模式（Guest OS 内核）
- **EL2**: Hypervisor 模式（**Xen 运行在此级别**）
- **EL3**: Secure Monitor 模式（安全监控）

### 4.2 Device Tree

ARM 架构传统上使用 **Device Tree (DT)** 描述硬件：
- 扁平化的设备树 (FDT - Flattened Device Tree)
- 包含 CPU、内存、中断控制器、设备等信息
- 在启动时由 bootloader 传递给 Xen

### 4.3 GIC (Generic Interrupt Controller)

ARM 架构使用 GIC 处理中断：
- **GICv2**: 传统版本
- **GICv3**: 支持系统寄存器接口
- Domain 0 使用原生 GIC 配置

### 4.4 SMMU (System Memory Management Unit)

ARM 架构的 IOMMU 实现：
- 提供设备 DMA 地址转换
- 支持多级页表
- 与 CPU MMU 类似但用于设备

### 4.5 PSCI (Power State Coordination Interface)

ARM 架构的 CPU 电源管理协议：
- 用于启动/关闭 CPU
- 用于 CPU 挂起/恢复
- 标准化的固件接口

## 五、总结

ARM 架构特定的启动流程特点：

1. **汇编层面**:
   - 必须在 EL2 模式运行
   - 需要配置 ARM 特定的系统寄存器（MAIR、TCR、SCTLR）
   - 需要计算物理地址偏移

2. **C 语言层面**:
   - 使用 Device Tree 描述硬件
   - 使用 GIC 处理中断
   - 使用 SMMU 作为 IOMMU
   - 使用 PSCI 进行 CPU 管理

3. **Domain 0 创建**:
   - 需要映射 GIC 到 Domain 0
   - 需要准备设备树给 Domain 0
   - 支持平台特定的设备映射

## 六、参考

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [Device Tree Specification](https://www.devicetree.org/specifications/)
- [GIC Architecture Specification](https://developer.arm.com/documentation/ihi0048/latest/)
- `xen/xen/arch/arm/arm64/head.S`
- `xen/xen/arch/arm/setup.c`
