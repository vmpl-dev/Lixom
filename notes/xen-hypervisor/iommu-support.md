# Xen IOMMU 支持详解

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/drivers/passthrough/iommu.c` - IOMMU 核心实现
- `xen/xen/drivers/passthrough/x86/iommu.c` - x86 IOMMU 架构支持
- `xen/xen/drivers/passthrough/vtd/` - Intel VT-d 实现
- `xen/xen/drivers/passthrough/amd/` - AMD-Vi 实现
- `xen/xen/drivers/passthrough/arm/` - ARM SMMU 实现
- `xen/xen/include/xen/iommu.h` - IOMMU 接口定义
- `xen/docs/misc/xen-command-line.pandoc` - 命令行选项文档

## 概述

**IOMMU (I/O Memory Management Unit)** 是 Xen 中实现 PCI Passthrough 和 DMA 保护的关键组件。IOMMU 类似于 CPU 的 MMU，但用于 I/O 设备的地址转换和访问控制。

**核心观点**: IOMMU 提供两个主要功能：
1. **DMA 重映射**: 将设备虚拟地址（DFN）映射到系统物理地址（MFN）
2. **中断重映射**: 控制 MSI 中断的路由和传递

## 一、IOMMU 架构支持

### 1.1 支持的 IOMMU 类型

Xen 支持多种 IOMMU 架构：

#### 1.1.1 Intel VT-d (Virtualization Technology for Directed I/O)

**位置**: `xen/xen/drivers/passthrough/vtd/`

**特点**:
- ✅ DMA 重映射
- ✅ 中断重映射
- ✅ 队列化无效化（Queued Invalidation）
- ✅ 窥探控制（Snoop Control）
- ✅ 支持超级页（Superpages）

**配置**: `CONFIG_INTEL_IOMMU`

**参考**: `xen/xen/drivers/passthrough/Kconfig:52-62`

```52:62:xen/xen/drivers/passthrough/Kconfig
config INTEL_IOMMU
	bool "Intel VT-d" if EXPERT
	depends on X86
	default y
	help
	  Enables I/O virtualization on platforms that implement the
	  Intel Virtualization Technology for Directed I/O (Intel VT-d).

	  If your system includes an IOMMU implementing Intel VT-d, say Y.
	  This is required if your system has more than 254 CPUs.
	  If in doubt, say Y.
```

#### 1.1.2 AMD-Vi (AMD I/O Virtualization)

**位置**: `xen/xen/drivers/passthrough/amd/`

**特点**:
- ✅ DMA 重映射
- ✅ 中断重映射（可选）
- ✅ 支持超级页
- ⚠️ 不支持共享页表（sharept）

**配置**: `CONFIG_AMD_IOMMU`

**参考**: `xen/xen/drivers/passthrough/Kconfig:40-50`

```40:50:xen/xen/drivers/passthrough/Kconfig
config AMD_IOMMU
	bool "AMD IOMMU" if EXPERT
	depends on X86
	default y
	help
	  Enables I/O virtualization on platforms that implement the
	  AMD I/O Virtualization Technology (IOMMU).

	  If your system includes an IOMMU implementing AMD-Vi, say Y.
	  This is required if your system has more than 254 CPUs.
	  If in doubt, say Y.
```

#### 1.1.3 ARM SMMU (System Memory Management Unit)

**位置**: `xen/xen/drivers/passthrough/arm/`

**支持版本**:
- **SMMUv1/v2**: `CONFIG_ARM_SMMU`
- **SMMUv3**: `CONFIG_ARM_SMMU_V3` (实验性)
- **IPMMU-VMSA**: `CONFIG_IPMMU_VMSA` (Renesas R-Car)

**特点**:
- ✅ DMA 重映射
- ✅ 支持设备树绑定
- ✅ 页表始终共享（与 HAP）

**参考**: `xen/xen/drivers/passthrough/Kconfig:6-37`

### 1.2 IOMMU 操作接口

**统一接口结构**:

```260:323:xen/xen/include/xen/iommu.h
struct iommu_ops {
    unsigned long page_sizes;
    int (*init)(struct domain *d);
    void (*hwdom_init)(struct domain *d);
    int (*quarantine_init)(device_t *dev, bool scratch_page);
    int (*add_device)(u8 devfn, device_t *dev);
    int (*enable_device)(device_t *dev);
    int (*remove_device)(u8 devfn, device_t *dev);
    int (*assign_device)(struct domain *, u8 devfn, device_t *dev, u32 flag);
    int (*reassign_device)(struct domain *s, struct domain *t,
                           u8 devfn, device_t *dev);
#ifdef CONFIG_HAS_PCI
    int (*get_device_group_id)(u16 seg, u8 bus, u8 devfn);
#endif /* HAS_PCI */

    void (*teardown)(struct domain *d);

    /*
     * This block of operations must be appropriately locked against each
     * other by the caller in order to have meaningful results.
     */
    int __must_check (*map_page)(struct domain *d, dfn_t dfn, mfn_t mfn,
                                 unsigned int flags,
                                 unsigned int *flush_flags);
    int __must_check (*unmap_page)(struct domain *d, dfn_t dfn,
                                   unsigned int order,
                                   unsigned int *flush_flags);
    int __must_check (*lookup_page)(struct domain *d, dfn_t dfn, mfn_t *mfn,
                                    unsigned int *flags);

#ifdef CONFIG_X86
    int (*enable_x2apic)(void);
    void (*disable_x2apic)(void);

    void (*update_ire_from_apic)(unsigned int apic, unsigned int pin,
                                 uint64_t rte);
    unsigned int (*read_apic_from_ire)(unsigned int apic, unsigned int reg);

    int (*setup_hpet_msi)(struct msi_desc *);

    void (*adjust_irq_affinities)(void);
    void (*clear_root_pgtable)(struct domain *d);
    int (*update_ire_from_msi)(struct msi_desc *msi_desc, struct msi_msg *msg);
#endif /* CONFIG_X86 */

    int __must_check (*suspend)(void);
    void (*resume)(void);
    void (*crash_shutdown)(void);
    int __must_check (*iotlb_flush)(struct domain *d, dfn_t dfn,
                                    unsigned long page_count,
                                    unsigned int flush_flags);
    int (*get_reserved_device_memory)(iommu_grdm_t *, void *);
    void (*dump_page_tables)(struct domain *d);

#ifdef CONFIG_HAS_DEVICE_TREE
    /*
     * All IOMMU drivers which support generic IOMMU DT bindings should use
     * this callback. This is a way for the framework to provide the driver
     * with DT IOMMU specifier which describes the IOMMU master interfaces of
     * that device (device IDs, etc).
     */
    int (*dt_xlate)(device_t *dev, const struct dt_phandle_args *args);
#endif
};
```

**主要操作**:
- `init`: Domain IOMMU 初始化
- `hwdom_init`: Domain 0 IOMMU 初始化
- `add_device`: 添加设备到 IOMMU
- `assign_device`: 分配设备到 Domain
- `map_page`/`unmap_page`: 页映射/取消映射
- `iotlb_flush`: IOTLB 刷新

## 二、IOMMU 初始化流程

### 2.1 启动流程

**初始化顺序**:

```
1. 解析命令行参数 (parse_iommu_param)
   ↓
2. ACPI 初始化 (acpi_iommu_init)
   ├─ Intel VT-d: acpi_dmar_init()
   └─ AMD-Vi: acpi_ivrs_init()
   ↓
3. 硬件设置 (iommu_hardware_setup)
   ├─ 扫描 PCI 设备 (scan_pci_devices)
   ├─ 保存 IO-APIC 设置 (如果启用中断重映射)
   └─ 调用平台特定的 setup()
   ↓
4. IOMMU 设置 (iommu_setup)
   ├─ 验证页大小
   ├─ 初始化隔离 (iommu_quarantine_init)
   └─ 打印配置信息
   ↓
5. Domain 0 初始化 (iommu_hwdom_init)
   └─ 建立 1:1 映射
```

### 2.2 初始化代码

**IOMMU 设置**:

```548:607:xen/xen/drivers/passthrough/iommu.c
int __init iommu_setup(void)
{
    int rc = -ENODEV;
    bool force_intremap = force_iommu && iommu_intremap;

    if ( iommu_hwdom_strict )
        iommu_hwdom_passthrough = false;

    if ( iommu_enable )
    {
        const struct iommu_ops *ops = NULL;

        rc = iommu_hardware_setup();
        if ( !rc )
            ops = iommu_get_ops();
        if ( ops && (ops->page_sizes & -ops->page_sizes) != PAGE_SIZE )
        {
            printk(XENLOG_ERR "IOMMU: page size mask %lx unsupported\n",
                   ops->page_sizes);
            rc = ops->page_sizes ? -EPERM : -ENODATA;
        }
        iommu_enabled = (rc == 0);
    }

#ifndef iommu_intremap
    if ( !iommu_enabled )
        iommu_intremap = iommu_intremap_off;
#endif

    if ( (force_iommu && !iommu_enabled) ||
         (force_intremap && !iommu_intremap) )
        panic("Couldn't enable %s and iommu=required/force\n",
              !iommu_enabled ? "IOMMU" : "Interrupt Remapping");

#ifndef iommu_intpost
    if ( !iommu_intremap )
        iommu_intpost = false;
#endif

    printk("I/O virtualisation %sabled\n", iommu_enabled ? "en" : "dis");
    if ( !iommu_enabled )
    {
        iommu_hwdom_passthrough = false;
        iommu_hwdom_strict = false;
    }
    else
    {
        if ( iommu_quarantine_init() )
            panic("Could not set up quarantine\n");

        printk(" - Dom0 mode: %s\n",
               iommu_hwdom_passthrough ? "Passthrough" :
               iommu_hwdom_strict ? "Strict" : "Relaxed");
#ifndef iommu_intremap
        printk("Interrupt remapping %sabled\n", iommu_intremap ? "en" : "dis");
#endif
    }

    return rc;
}
```

**硬件设置**:

```86:140:xen/xen/drivers/passthrough/x86/iommu.c
int __init iommu_hardware_setup(void)
{
    struct IO_APIC_route_entry **ioapic_entries = NULL;
    int rc;

    if ( !iommu_init_ops )
        return -ENODEV;

    rc = scan_pci_devices();
    if ( rc )
        return rc;

    if ( !iommu_ops.init )
        iommu_ops = *iommu_init_ops->ops;
    else
        /* x2apic setup may have previously initialised the struct. */
        ASSERT(iommu_ops.init == iommu_init_ops->ops->init);

    if ( !x2apic_enabled && iommu_intremap )
    {
        /*
         * If x2APIC is enabled interrupt remapping is already enabled, so
         * there's no need to mess with the IO-APIC because the remapping
         * entries are already correctly setup by x2apic_bsp_setup.
         */
        ioapic_entries = alloc_ioapic_entries();
        if ( !ioapic_entries )
            return -ENOMEM;
        rc = save_IO_APIC_setup(ioapic_entries);
        if ( rc )
        {
            free_ioapic_entries(ioapic_entries);
            return rc;
        }

        mask_8259A();
        mask_IO_APIC_setup(ioapic_entries);
    }

    if ( !iommu_superpages )
        iommu_ops.page_sizes &= PAGE_SIZE_4K;

    rc = iommu_init_ops->setup();

    ASSERT(iommu_superpages || iommu_ops.page_sizes == PAGE_SIZE_4K);

    if ( ioapic_entries )
    {
        restore_IO_APIC_setup(ioapic_entries, rc);
        unmask_8259A();
        free_ioapic_entries(ioapic_entries);
    }

    return rc;
}
```

### 2.3 Domain IOMMU 初始化

**Domain 初始化**:

```192:236:xen/xen/drivers/passthrough/iommu.c
int iommu_domain_init(struct domain *d, unsigned int opts)
{
    struct domain_iommu *hd = dom_iommu(d);
    int ret = 0;

    if ( is_hardware_domain(d) )
        check_hwdom_reqs(d); /* may modify iommu_hwdom_strict */

    if ( !is_iommu_enabled(d) )
        return 0;

#ifdef CONFIG_NUMA
    hd->node = NUMA_NO_NODE;
#endif

    ret = arch_iommu_domain_init(d);
    if ( ret )
        return ret;

    hd->platform_ops = iommu_get_ops();
    ret = iommu_call(hd->platform_ops, init, d);
    if ( ret || is_system_domain(d) )
        return ret;

    /*
     * Use shared page tables for HAP and IOMMU if the global option
     * is enabled (from which we can infer the h/w is capable) and
     * the domain options do not disallow it. HAP must, of course, also
     * be enabled.
     */
    hd->hap_pt_share = hap_enabled(d) && iommu_hap_pt_share &&
        !(opts & XEN_DOMCTL_IOMMU_no_sharept);

    /*
     * NB: 'relaxed' h/w domains don't need the IOMMU mappings to be kept
     *     in-sync with their assigned pages because all host RAM will be
     *     mapped during hwdom_init().
     */
    if ( !is_hardware_domain(d) || iommu_hwdom_strict )
        hd->need_sync = !iommu_use_hap_pt(d);

    ASSERT(!(hd->need_sync && hd->hap_pt_share));

    return 0;
}
```

**关键配置**:
- **hap_pt_share**: 是否与 HAP 共享页表
- **need_sync**: 是否需要与 P2M 同步

## 三、IOMMU 映射模式

### 3.1 映射模式类型

#### 3.1.1 共享页表模式 (share_pt)

**定义**: IOMMU 页表与 HAP 页表共享

**特点**:
- ✅ 减少内存开销
- ✅ 提高性能
- ⚠️ 不支持基于页错误的特性（如脏页跟踪）

**适用场景**:
- HVM/PVH Domain
- 无 PCI Passthrough 设备
- 或使用 CPU 页表共享

**配置**:
```bash
iommu=sharept
```

**参考**: `xen/docs/misc/xen-command-line.pandoc:1549-1559`

#### 3.1.2 同步模式 (sync_pt)

**定义**: IOMMU 映射与 Domain 的 P2M 表同步

**PV Domain**:
- 所有可写页按 MFN 进行恒等映射
- 设备驱动可以使用 MFN 进行 DMA

**HVM/PVH Domain**:
- 所有非外部 RAM 页按 GFN 映射
- 设备驱动可以使用 GFN 进行 DMA

**参考**: `xen/docs/man/xl.cfg.5.pod.in:640-654`

#### 3.1.3 Passthrough 模式

**定义**: Domain 0 的 IOMMU 映射模式

**模式**:
- **passthrough**: 所有内存 1:1 映射（无保护）
- **strict**: 仅映射常规 RAM（有保护）
- **relaxed**: 映射常规 RAM + 保留区域（默认）

**配置**:
```bash
dom0-iommu=passthrough  # 或 strict, relaxed
```

### 3.2 页映射实现

**映射函数**:

```324:380:xen/xen/drivers/passthrough/iommu.c
long iommu_map(struct domain *d, dfn_t dfn0, mfn_t mfn0,
               unsigned long page_count, unsigned int flags,
               unsigned int *flush_flags)
{
    const struct domain_iommu *hd = dom_iommu(d);
    unsigned long i;
    unsigned int order, j = 0;
    int rc = 0;

    if ( !is_iommu_enabled(d) )
        return 0;

    ASSERT(!IOMMUF_order(flags));

    for ( i = 0; i < page_count; i += 1UL << order )
    {
        dfn_t dfn = dfn_add(dfn0, i);
        mfn_t mfn = mfn_add(mfn0, i);

        order = mapping_order(hd, dfn, mfn, page_count - i);

        if ( (flags & IOMMUF_preempt) &&
             ((!(++j & 0xfff) && general_preempt_check()) ||
              i > LONG_MAX - (1UL << order)) )
            return i;

        rc = iommu_call(hd->platform_ops, map_page, d, dfn, mfn,
                        flags | IOMMUF_order(order), flush_flags);

        if ( likely(!rc) )
            continue;

        if ( !d->is_shutting_down && printk_ratelimit() )
            printk(XENLOG_ERR
                   "d%d: IOMMU mapping dfn %"PRI_dfn" to mfn %"PRI_mfn" failed: %d\n",
                   d->domain_id, dfn_x(dfn), mfn_x(mfn), rc);

        /* while statement to satisfy __must_check */
        while ( iommu_unmap(d, dfn0, i, 0, flush_flags) )
            break;

        if ( !is_hardware_domain(d) )
            domain_crash(d);

        break;
    }

    /*
     * Something went wrong so, if we were dealing with more than a single
     * page, flush everything and clear flush flags.
     */
    if ( page_count > 1 && unlikely(rc) &&
         !iommu_iotlb_flush_all(d, *flush_flags) )
        *flush_flags = 0;

    return rc;
}
```

**映射顺序计算**:

```299:322:xen/xen/drivers/passthrough/iommu.c
static unsigned int mapping_order(const struct domain_iommu *hd,
                                  dfn_t dfn, mfn_t mfn, unsigned long nr)
{
    unsigned long res = dfn_x(dfn) | mfn_x(mfn);
    unsigned long sizes = hd->platform_ops->page_sizes;
    unsigned int bit = find_first_set_bit(sizes), order = 0;

    ASSERT(bit == PAGE_SHIFT);

    while ( (sizes = (sizes >> bit) & ~1) )
    {
        unsigned long mask;

        bit = find_first_set_bit(sizes);
        mask = (1UL << bit) - 1;
        if ( nr <= mask || (res & mask) )
            break;
        order += bit;
        nr >>= bit;
        res >>= bit;
    }

    return order;
}
```

**功能**:
- 计算最大可用超级页大小
- 优化映射性能（减少页表项数量）

## 四、Domain 0 IOMMU 配置

### 4.1 配置模式

**三种模式**:

1. **Passthrough 模式**:
   - 所有内存 1:1 映射
   - 无 DMA 保护
   - 性能最高

2. **Strict 模式**:
   - 仅映射常规 RAM
   - 提供 DMA 保护
   - 需要 RMRR/IVMD 处理保留区域

3. **Relaxed 模式** (默认):
   - 映射常规 RAM + 保留区域
   - 平衡性能和安全性

**配置函数**:

```303:371:xen/xen/drivers/passthrough/x86/iommu.c
static unsigned int __hwdom_init hwdom_iommu_map(const struct domain *d,
                                                 unsigned long pfn,
                                                 unsigned long max_pfn)
{
    mfn_t mfn = _mfn(pfn);
    unsigned int i, type, perms = IOMMUF_readable | IOMMUF_writable;

    /*
     * Set up 1:1 mapping for dom0. Default to include only conventional RAM
     * areas and let RMRRs include needed reserved regions. When set, the
     * inclusive mapping additionally maps in every pfn up to 4GB except those
     * that fall in unusable ranges for PV Dom0.
     */
    if ( (pfn > max_pfn && !mfn_valid(mfn)) || xen_in_range(pfn) )
        return 0;

    switch ( type = page_get_ram_type(mfn) )
    {
    case RAM_TYPE_UNUSABLE:
        return 0;

    case RAM_TYPE_CONVENTIONAL:
        if ( iommu_hwdom_strict )
            return 0;
        break;

    default:
        if ( type & RAM_TYPE_RESERVED )
        {
            if ( !iommu_hwdom_inclusive && !iommu_hwdom_reserved )
                perms = 0;
        }
        else if ( is_hvm_domain(d) )
            return 0;
        else if ( !iommu_hwdom_inclusive || pfn > max_pfn )
            perms = 0;
    }

    /* Check that it doesn't overlap with the Interrupt Address Range. */
    if ( pfn >= 0xfee00 && pfn <= 0xfeeff )
        return 0;
    /* ... or the IO-APIC */
    if ( has_vioapic(d) )
    {
        for ( i = 0; i < d->arch.hvm.nr_vioapics; i++ )
            if ( pfn == PFN_DOWN(domain_vioapic(d, i)->base_address) )
                return 0;
    }
    else if ( is_pv_domain(d) )
    {
        /*
         * Be consistent with CPU mappings: Dom0 is permitted to establish r/o
         * ones there (also for e.g. HPET in certain cases), so it should also
         * have such established for IOMMUs.
         */
        if ( iomem_access_permitted(d, pfn, pfn) &&
             rangeset_contains_singleton(mmio_ro_ranges, pfn) )
            perms = IOMMUF_readable;
    }
    /*
     * ... or the PCIe MCFG regions.
     * TODO: runtime added MMCFG regions are not checked to make sure they
     * don't overlap with already mapped regions, thus preventing trapping.
     */
    if ( has_vpci(d) && vpci_is_mmcfg_address(d, pfn_to_paddr(pfn)) )
        return 0;

    return perms;
}
```

### 4.2 配置选项

**命令行参数**:

```bash
# Domain 0 IOMMU 模式
dom0-iommu=passthrough  # 或 strict, relaxed, none

# 旧参数（已弃用）
iommu=dom0-passthrough
iommu=dom0-strict
```

**解析函数**:

```149:179:xen/xen/drivers/passthrough/iommu.c
static int __init cf_check parse_dom0_iommu_param(const char *s)
{
    const char *ss;
    int rc = 0;

    do {
        int val;

        ss = strchr(s, ',');
        if ( !ss )
            ss = strchr(s, '\0');

        if ( (val = parse_boolean("passthrough", s, ss)) >= 0 )
            iommu_hwdom_passthrough = val;
        else if ( (val = parse_boolean("strict", s, ss)) >= 0 )
            iommu_hwdom_strict = val;
        else if ( (val = parse_boolean("map-inclusive", s, ss)) >= 0 )
            iommu_hwdom_inclusive = val;
        else if ( (val = parse_boolean("map-reserved", s, ss)) >= 0 )
            iommu_hwdom_reserved = val;
        else if ( !cmdline_strcmp(s, "none") )
            iommu_hwdom_none = true;
        else
            rc = -EINVAL;

        s = ss + 1;
    } while ( *ss );

    return rc;
}
custom_param("dom0-iommu", parse_dom0_iommu_param);
```

## 五、中断重映射

### 5.1 概述

**中断重映射 (Interrupt Remapping)** 是 IOMMU 的第二个主要功能，用于控制 MSI 中断的路由。

**功能**:
- 控制 MSI 消息的路由
- 防止设备伪造中断
- 支持 x2APIC

**状态**:
- `iommu_intremap_off`: 禁用
- `iommu_intremap_restricted`: 受限（仅 8 位 APIC ID）
- `iommu_intremap_full`: 完全支持

**定义**:

```64:77:xen/xen/include/xen/iommu.h
extern enum __packed iommu_intremap {
   /*
    * In order to allow traditional boolean uses of the iommu_intremap
    * variable, the "off" value has to come first (yielding a value of zero).
    */
   iommu_intremap_off,
   /*
    * Interrupt remapping enabled, but only able to generate interrupts
    * with an 8-bit APIC ID.
    */
   iommu_intremap_restricted,
   iommu_intremap_full,
} iommu_intremap;
```

### 5.2 Posted Interrupt

**Posted Interrupt (intpost)**: 允许 IOMMU 直接将中断投递到 Guest，无需陷入 Hypervisor。

**特点**:
- ✅ 降低中断延迟
- ✅ 需要 APIC 加速（VT-x APICV / SVM AVIC）
- ⚠️ 默认禁用（存在极端情况问题）

**配置**:
```bash
iommu=intremap,intpost
```

**参考**: `xen/docs/misc/xen-command-line.pandoc:1574-1582`

## 六、设备隔离

### 6.1 隔离机制

**隔离 (Quarantine)**: 当设备从 Guest 解除分配时，防止在途 DMA 访问主机内存。

**隔离类型**:

1. **None**: 无隔离
2. **Basic**: 所有 DMA 触发 IOMMU 错误
3. **Scratch Page**: 写入触发错误，读取指向临时页

**配置选项**:

```34:48:xen/xen/drivers/passthrough/iommu.c
#define IOMMU_quarantine_none         0 /* aka false */
#define IOMMU_quarantine_basic        1 /* aka true */
#define IOMMU_quarantine_scratch_page 2
#ifdef CONFIG_HAS_PCI
uint8_t __read_mostly iommu_quarantine =
# if defined(CONFIG_IOMMU_QUARANTINE_NONE)
    IOMMU_quarantine_none;
# elif defined(CONFIG_IOMMU_QUARANTINE_BASIC)
    IOMMU_quarantine_basic;
# elif defined(CONFIG_IOMMU_QUARANTINE_SCRATCH_PAGE)
    IOMMU_quarantine_scratch_page;
# endif
#else
# define iommu_quarantine IOMMU_quarantine_none
#endif /* CONFIG_HAS_PCI */
```

**命令行配置**:
```bash
iommu=quarantine          # Basic 隔离
iommu=quarantine=scratch-page  # Scratch page 隔离
iommu=quarantine=0       # 禁用隔离
```

## 七、IOMMU 配置选项

### 7.1 通用选项

**基本选项**:

```bash
# 启用/禁用
iommu=1          # 启用（默认）
iommu=0          # 禁用

# 强制启用
iommu=force      # 或 required，失败则 panic

# 调试
iommu=verbose     # 详细输出
iommu=debug       # 调试模式
```

**参考**: `xen/docs/misc/xen-command-line.pandoc:1482-1520`

### 7.2 Intel VT-d 特定选项

**选项**:

```bash
# 窥探控制
iommu=snoop      # 启用（默认）

# 队列化无效化
iommu=qinval     # 启用（默认）

# 集成显卡
iommu=igfx       # 启用（默认）
iommu=no-igfx    # 禁用（用于调试）
```

**参考**: `xen/docs/misc/xen-command-line.pandoc:1590-1615`

### 7.3 AMD-Vi 特定选项

**选项**:

```bash
# 每设备中断重映射表
iommu=amd-iommu-perdev-intremap  # 启用（默认，安全）
iommu=no-amd-iommu-perdev-intremap  # 全局表（不安全，仅用于 SP5100 错误修复）
```

**参考**: `xen/docs/misc/xen-command-line.pandoc:1616-1625`

### 7.4 其他选项

**超级页**:
```bash
iommu=superpages  # 启用（默认）
iommu=no-superpages  # 禁用
```

**共享页表**:
```bash
iommu=sharept     # 启用（Intel 默认）
iommu=no-sharept    # 禁用
```

**设备 IOTLB 超时**:
```bash
iommu_dev_iotlb_timeout=2000  # 毫秒（默认 1000）
```

## 八、代码结构

### 8.1 文件组织

```
xen/xen/drivers/passthrough/
├── iommu.c              # IOMMU 核心实现
├── x86/
│   └── iommu.c          # x86 架构支持
├── vtd/                 # Intel VT-d
│   ├── iommu.c          # VT-d 实现
│   ├── dmar.c           # DMAR 表解析
│   ├── intremap.c       # 中断重映射
│   └── qinval.c         # 队列化无效化
├── amd/                 # AMD-Vi
│   ├── iommu_init.c     # 初始化
│   ├── iommu_map.c      # 映射实现
│   ├── iommu_intr.c     # 中断处理
│   └── pci_amd_iommu.c  # PCI 集成
└── arm/                 # ARM SMMU
    ├── iommu.c          # SMMU 核心
    ├── smmu.c           # SMMUv1/v2
    ├── smmu-v3.c        # SMMUv3
    └── ipmmu-vmsa.c     # IPMMU-VMSA
```

### 8.2 数据结构

**Domain IOMMU 结构**:

```346:377:xen/xen/include/xen/iommu.h
struct domain_iommu {
    struct arch_iommu arch;

    /* iommu_ops */
    const struct iommu_ops *platform_ops;

#ifdef CONFIG_HAS_DEVICE_TREE
    /* List of DT devices assigned to this domain */
    struct list_head dt_devices;
#endif

#ifdef CONFIG_NUMA
    /* NUMA node to do IOMMU related allocations against. */
    nodeid_t node;
#endif

    /* Features supported by the IOMMU */
    /* SAF-2-safe enum constant in arithmetic operation */
    DECLARE_BITMAP(features, IOMMU_FEAT_count);

    /* Does the guest share HAP mapping with the IOMMU? */
    bool hap_pt_share;

    /*
     * Does the guest require mappings to be synchronized, to maintain
     * the default dfn == pfn map? (See comment on dfn at the top of
     * include/xen/mm.h). Note that hap_pt_share == false does not
     * necessarily imply this is true.
     */
    bool need_sync;
};
```

**架构特定结构** (x86):

```34:58:xen/xen/arch/x86/include/asm/iommu.h
struct arch_iommu
{
    spinlock_t mapping_lock; /* io page table lock */
    struct {
        struct page_list_head list;
        spinlock_t lock;
    } pgtables;

    struct list_head identity_maps;

    union {
        /* Intel VT-d */
        struct {
            uint64_t pgd_maddr; /* io page directory machine address */
            unsigned int agaw; /* adjusted guest address width, 0 is level 2 30-bit */
            unsigned long *iommu_bitmap; /* bitmap of iommu(s) that the domain uses */
        } vtd;
        /* AMD IOMMU */
        struct {
            unsigned int paging_mode;
            struct page_info *root_table;
            struct guest_iommu *g_iommu;
        } amd;
    };
};
```

## 九、最佳实践

### 9.1 配置建议

**生产环境**:
```bash
# 启用 IOMMU
iommu=1

# Domain 0 使用 Strict 模式（安全）
dom0-iommu=strict

# 启用隔离（安全）
iommu=quarantine

# 启用中断重映射（安全）
iommu=intremap
```

**性能优化**:
```bash
# 启用共享页表（如果支持）
iommu=sharept

# 启用超级页
iommu=superpages
```

**调试**:
```bash
# 详细输出
iommu=verbose,debug

# 禁用集成显卡 IOMMU（如果遇到问题）
iommu=no-igfx
```

### 9.2 故障排查

**常见问题**:

1. **IOMMU 未启用**:
   - 检查 BIOS 设置
   - 检查命令行参数
   - 查看启动日志

2. **设备分配失败**:
   - 检查 IOMMU 是否启用
   - 检查设备是否在隔离组中
   - 检查 Domain 是否支持 Passthrough

3. **性能问题**:
   - 启用共享页表
   - 启用超级页
   - 检查 IOTLB 刷新频率

## 十、总结

### 10.1 核心要点

1. **IOMMU 功能**: DMA 重映射 + 中断重映射
2. **架构支持**: Intel VT-d, AMD-Vi, ARM SMMU
3. **映射模式**: 共享页表、同步模式、Passthrough
4. **Domain 0 模式**: Passthrough、Strict、Relaxed
5. **隔离机制**: 保护主机免受恶意 DMA

### 10.2 关键配置

- **启用**: `iommu=1`
- **Domain 0 模式**: `dom0-iommu=strict`
- **隔离**: `iommu=quarantine`
- **中断重映射**: `iommu=intremap`

### 10.3 适用场景

**必须启用**:
- PCI Passthrough
- 多 CPU 系统（>254 CPUs）
- 需要 DMA 保护

**建议启用**:
- 生产环境
- 多租户环境
- 安全敏感应用

## 十一、参考

- `xen/xen/drivers/passthrough/` - IOMMU 实现
- `xen/xen/include/xen/iommu.h` - IOMMU 接口
- `xen/docs/misc/xen-command-line.pandoc` - 命令行选项
- `xen/docs/misc/vtd.txt` - VT-d 配置指南
- [GPU 虚拟化文档](./gpu-virtualization.md) - GPU Passthrough 相关
- [PCI Passthrough 文档](./domainU-configuration-guide.md) - 设备分配相关
