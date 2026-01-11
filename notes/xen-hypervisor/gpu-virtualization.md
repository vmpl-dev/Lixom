# Xen GPU 虚拟化详解

**日期**: 2026-01-01
**相关文件**:
- `xen/tools/libs/light/libxl_pci.c` - PCI 设备管理
- `xen/tools/libs/light/libxl_dm.c` - Device Model 配置
- `xen/tools/libs/light/libxl_types.idl` - 类型定义
- `xen/xen/drivers/passthrough/` - PCI Passthrough 驱动
- `xen/xen/arch/x86/hvm/stdvga.c` - 标准 VGA 模拟
- `xen/docs/man/xl.cfg.5.pod.in` - 配置文档
- `xen/docs/misc/vtd.txt` - VT-d 配置文档

## 概述

Xen 支持多种 GPU 虚拟化方式，主要包括：

1. **GPU Passthrough (gfx_passthru)**: 将物理 GPU 直接分配给 Guest Domain
2. **标准 PCI Passthrough**: 将 GPU 作为普通 PCI 设备透传
3. **虚拟显示接口 (displif)**: 基于 Xen 的虚拟显示协议
4. **QEMU 模拟 VGA**: 软件模拟的图形适配器

**核心观点**: GPU 虚拟化主要通过 PCI Passthrough 实现，需要 IOMMU 支持以确保 DMA 安全。

## 一、GPU Passthrough (gfx_passthru)

### 1.1 功能概述

**gfx_passthru** 是 Xen 专门为图形设备设计的 Passthrough 功能，使物理 GPU 成为 Guest 的主显示设备。

**特点**:
- ✅ 将物理 GPU 设为主显示设备
- ✅ 禁用 QEMU 模拟的图形适配器
- ✅ 禁用 VNC 控制台（图形输出）
- ✅ 透传 VGA 内存范围、BAR、MMIO、I/O 端口
- ✅ 复制并执行 VBIOS（视频 BIOS）
- ⚠️ **不支持 GPU 共享** - 一次只能分配给一个 VM

**配置选项**:

```1158:1216:xen/docs/man/xl.cfg.5.pod.in
=item B<gfx_passthru=BOOLEAN|"STRING">

Enable graphics device PCI passthrough. This option makes an assigned
PCI graphics card become the primary graphics card in the VM. The QEMU
emulated graphics adapter is disabled and the VNC console for the VM
will not have any graphics output. All graphics output, including boot
time QEMU BIOS messages from the VM, will go to the physical outputs
of the passed through physical graphics card.

The graphics card PCI device to pass through is chosen with the B<pci>
option, in exactly the same way a normal Xen PCI device
passthrough/assignment is done.  Note that B<gfx_passthru> does not do
any kind of sharing of the GPU, so you can assign the GPU to only one
single VM at a time.

B<gfx_passthru> also enables various legacy VGA memory ranges, BARs, MMIOs,
and ioports to be passed through to the VM, since those are required
for correct operation of things like VGA BIOS, text mode, VBE, etc.

Enabling the B<gfx_passthru> option also copies the physical graphics card
video BIOS to the guest memory, and executes the VBIOS in the guest
to initialize the graphics card.

Most graphics adapters require vendor specific tweaks for properly
working graphics passthrough. See the XenVGAPassthroughTestedAdapters
L<https://wiki.xenproject.org/wiki/XenVGAPassthroughTestedAdapters> wiki page
for graphics cards currently supported by B<gfx_passthru>.

B<gfx_passthru> is currently supported both with the qemu-xen-traditional
device-model and upstream qemu-xen device-model.

When given as a boolean the B<gfx_passthru> option either disables graphics
card passthrough or enables autodetection.

When given as a string the B<gfx_passthru> option describes the type
of device to enable. Note that this behavior is only supported with the
upstream qemu-xen device-model. With qemu-xen-traditional IGD (Intel Graphics
Device) is always assumed and options other than autodetect or explicit IGD
will result in an error.

Currently, valid values for the option are:

=over 4

=item B<0>

Disables graphics device PCI passthrough.

=item B<1>, B<"default">

Enables graphics device PCI passthrough and autodetects the type of device
which is being used.

=item B<"igd">

Enables graphics device PCI passthrough but forcing the type of device to
Intel Graphics Device.

=back
```

### 1.2 类型定义

**枚举类型**:

```184:187:xen/tools/libs/light/libxl_types.idl
libxl_gfx_passthru_kind = Enumeration("gfx_passthru_kind", [
    (0, "default"),
    (1, "igd"),
    ])
```

**类型说明**:
- `default` (0): 自动检测 GPU 类型
- `igd` (1): 强制使用 Intel Graphics Device

### 1.3 实现流程

#### 1.3.1 配置解析

**解析 gfx_passthru 配置**:

```2811:2822:xen/tools/xl/xl_parse.c
        if (!xlu_cfg_get_long(config, "gfx_passthru", &l, 1)) {
            libxl_defbool_set(&b_info->u.hvm.gfx_passthru, l);
        } else if (!xlu_cfg_get_string(config, "gfx_passthru", &buf, 0)) {
            if (libxl_gfx_passthru_kind_from_string(buf,
                                        &b_info->u.hvm.gfx_passthru_kind)) {
                fprintf(stderr,
                        "ERROR: invalid value \"%s\" for \"gfx_passthru\"\n",
                        buf);
                exit (1);
            }
            libxl_defbool_set(&b_info->u.hvm.gfx_passthru, true);
        }
```

**流程**:
1. 尝试解析为整数（布尔值）
2. 如果失败，尝试解析为字符串（类型）
3. 验证字符串是否为有效的类型值

#### 1.3.2 类型检测

**自动检测 GPU 类型**:

```999:1013:xen/tools/libs/light/libxl_dm.c
static enum libxl_gfx_passthru_kind
libxl__detect_gfx_passthru_kind(libxl__gc *gc,
                                const libxl_domain_config *guest_config)
{
    const libxl_domain_build_info *b_info = &guest_config->b_info;

    if (b_info->u.hvm.gfx_passthru_kind != LIBXL_GFX_PASSTHRU_KIND_DEFAULT)
        return b_info->u.hvm.gfx_passthru_kind;

    if (libxl__is_igd_vga_passthru(gc, guest_config)) {
        return LIBXL_GFX_PASSTHRU_KIND_IGD;
    }

    return LIBXL_GFX_PASSTHRU_KIND_DEFAULT;
}
```

**检测逻辑**:
1. 如果已指定类型，直接返回
2. 检测是否为 Intel IGD
3. 否则返回默认类型

#### 1.3.3 Device Model 配置

**QEMU-Xen-Traditional**:

```904:914:xen/tools/libs/light/libxl_dm.c
        if (libxl_defbool_val(b_info->u.hvm.gfx_passthru)) {
            switch (b_info->u.hvm.gfx_passthru_kind) {
            case LIBXL_GFX_PASSTHRU_KIND_DEFAULT:
            case LIBXL_GFX_PASSTHRU_KIND_IGD:
                flexarray_append(dm_args, "-gfx_passthru");
                break;
            default:
                LOGD(ERROR, domid, "unsupported gfx_passthru_kind.");
                return ERROR_INVAL;
            }
        }
```

**QEMU-Xen (Upstream)**:

```1834:1848:xen/tools/libs/light/libxl_dm.c
        if (libxl_defbool_val(b_info->u.hvm.gfx_passthru)) {
            enum libxl_gfx_passthru_kind gfx_passthru_kind =
                            libxl__detect_gfx_passthru_kind(gc, guest_config);
            switch (gfx_passthru_kind) {
            case LIBXL_GFX_PASSTHRU_KIND_IGD:
                machinearg = GCSPRINTF("%s,igd-passthru=on", machinearg);
                break;
            case LIBXL_GFX_PASSTHRU_KIND_DEFAULT:
                LOGD(ERROR, guest_domid, "unable to detect required gfx_passthru_kind");
                return ERROR_FAIL;
            default:
                LOGD(ERROR, guest_domid, "invalid value for gfx_passthru_kind");
                return ERROR_INVAL;
            }
        }
```

**差异**:
- **qemu-xen-traditional**: 使用 `-gfx_passthru` 参数
- **qemu-xen**: 使用 `igd-passthru=on` 机器参数

#### 1.3.4 VGA IOMEM 权限

**授予 VGA 内存范围权限**:

```2542:2585:xen/tools/libs/light/libxl_pci.c
int libxl__grant_vga_iomem_permission(libxl__gc *gc, const uint32_t domid,
                                      libxl_domain_config *const d_config)
{
    int i, ret;

    if (!libxl_defbool_val(d_config->b_info.u.hvm.gfx_passthru))
        return 0;

    for (i = 0 ; i < d_config->num_pcidevs ; i++) {
        uint64_t vga_iomem_start = 0xa0000 >> XC_PAGE_SHIFT;
        uint32_t stubdom_domid;
        libxl_device_pci *pci = &d_config->pcidevs[i];
        unsigned long pci_device_class;

        if (sysfs_dev_get_class(gc, pci, &pci_device_class))
            continue;
        if (pci_device_class != 0x030000) /* VGA class */
            continue;

        stubdom_domid = libxl_get_stubdom_id(CTX, domid);
        ret = xc_domain_iomem_permission(CTX->xch, stubdom_domid,
                                         vga_iomem_start, 0x20, 1);
        if (ret < 0) {
            LOGED(ERROR, domid,
                  "failed to give stubdom%d access to iomem range "
                  "%"PRIx64"-%"PRIx64" for VGA passthru",
                  stubdom_domid,
                  vga_iomem_start, (vga_iomem_start + 0x20 - 1));
            return ret;
        }
        ret = xc_domain_iomem_permission(CTX->xch, domid,
                                         vga_iomem_start, 0x20, 1);
        if (ret < 0) {
            LOGED(ERROR, domid,
                  "failed to give dom%d access to iomem range "
                  "%"PRIx64"-%"PRIx64" for VGA passthru",
                  domid, vga_iomem_start, (vga_iomem_start + 0x20 - 1));
            return ret;
        }
        break;
    }

    return 0;
}
```

**功能**:
- 查找 PCI 设备类为 `0x030000`（VGA 类）的设备
- 授予 Stub Domain 和 Guest Domain 访问 VGA 内存范围（`0xa0000-0xbffff`）的权限
- 这是 VGA 文本模式和图形模式所需的内存范围

## 二、PCI Passthrough 基础

### 2.1 概述

**PCI Passthrough** 是将物理 PCI 设备直接分配给 Guest Domain 的技术。

**要求**:
- ✅ **IOMMU 支持**: Intel VT-d 或 AMD-Vi
- ✅ **设备隔离**: 设备必须从 Domain 0 中隐藏
- ✅ **DMA 保护**: IOMMU 确保 DMA 安全

### 2.2 IOMMU 配置

#### 2.2.1 启动参数

**启用 IOMMU**:

```bash
# Intel VT-d
iommu=1

# AMD-Vi
amd_iommu=on
```

**参考**: `xen/docs/misc/vtd.txt:26`

```26:26:xen/docs/misc/vtd.txt
        kernel /boot/xen.gz com1=115200,8n1 console=com1 iommu=1
```

#### 2.2.2 设备隐藏

**从 Domain 0 隐藏设备**:

**方法 1: 启动参数**

```bash
pciback.hide=(01:00.0)(03:00.0)
```

**方法 2: 动态隐藏（推荐）**

```bash
# 1. 检查设备驱动
ls -l /sys/bus/pci/devices/0000:01:00.0/driver

# 2. 卸载驱动
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/igb/unbind

# 3. 绑定到 pciback
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/pciback/new_slot
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/pciback/bind
```

**参考**: `xen/docs/misc/vtd.txt:30-39`

### 2.3 设备分配流程

#### 2.3.1 分配设备

**核心函数**:

```1407:1458:xen/xen/drivers/passthrough/pci.c
/* Caller should hold the pcidevs_lock */
static int assign_device(struct domain *d, u16 seg, u8 bus, u8 devfn, u32 flag)
{
    const struct domain_iommu *hd = dom_iommu(d);
    struct pci_dev *pdev;
    int rc = 0;

    if ( !is_iommu_enabled(d) )
        return 0;

    if ( !arch_iommu_use_permitted(d) )
        return -EXDEV;

    /* device_assigned() should already have cleared the device for assignment */
    ASSERT(pcidevs_locked());
    pdev = pci_get_pdev(NULL, PCI_SBDF(seg, bus, devfn));
    ASSERT(pdev && (pdev->domain == hardware_domain ||
                    pdev->domain == dom_io));

    /* Do not allow broken devices to be assigned to guests. */
    rc = -EBADF;
    if ( pdev->broken && d != hardware_domain && d != dom_io )
        goto done;

    rc = pdev_msix_assign(d, pdev);
    if ( rc )
        goto done;

    if ( pdev->domain != dom_io )
    {
        rc = iommu_quarantine_dev_init(pci_to_dev(pdev));
        if ( rc )
            goto done;
    }

    pdev->fault.count = 0;

    rc = iommu_call(hd->platform_ops, assign_device, d, devfn, pci_to_dev(pdev),
                    flag);

    while ( pdev->phantom_stride && !rc )
    {
        devfn += pdev->phantom_stride;
        if ( PCI_SLOT(devfn) != PCI_SLOT(pdev->devfn) )
            break;
        rc = iommu_call(hd->platform_ops, assign_device, d, devfn,
                        pdev->pci_to_dev(pdev), flag);
    }

 done:
    if ( rc )
    {
        printk
```

**流程**:
1. 检查 IOMMU 是否启用
2. 验证设备是否可分配
3. 分配 MSI-X 中断
4. 初始化 IOMMU 隔离
5. 调用平台特定的 `assign_device` 函数
6. 处理 Phantom 设备（如果存在）

#### 2.3.2 IOMMU 设备添加

**添加设备到 IOMMU**:

```1302:1331:xen/xen/drivers/passthrough/pci.c
static int iommu_add_device(struct pci_dev *pdev)
{
    const struct domain_iommu *hd;
    int rc;
    unsigned int devfn = pdev->devfn;

    if ( !pdev->domain )
        return -EINVAL;

    ASSERT(pcidevs_locked());

    hd = dom_iommu(pdev->domain);
    if ( !is_iommu_enabled(pdev->domain) )
        return 0;

    rc = iommu_call(hd->platform_ops, add_device, devfn, pci_to_dev(pdev));
    if ( rc || !pdev->phantom_stride )
        return rc;

    for ( ; ; )
    {
        devfn += pdev->phantom_stride;
        if ( PCI_SLOT(devfn) != PCI_SLOT(pdev->devfn) )
            return 0;
        rc = iommu_call(hd->platform_ops, add_device, devfn, pci_to_dev(pdev));
        if ( rc )
            printk(XENLOG_WARNING "IOMMU: add %pp failed (%d)\n",
                   &PCI_SBDF(pdev->seg, pdev->bus, devfn), rc);
    }
}
```

## 三、标准 VGA 模拟

### 3.1 概述

**标准 VGA 模拟** (`stdvga`) 是 Xen 提供的 VGA 设备模拟，用于提高性能。

**特点**:
- ✅ 缓存 VGA 状态
- ✅ 减少对 QEMU 的调用
- ✅ 支持标准 VGA 模式

**实现**: `xen/xen/arch/x86/hvm/stdvga.c`

### 3.2 VGA 内存范围

**定义**:

```37:38:xen/xen/arch/x86/hvm/stdvga.c
#define VGA_MEM_BASE 0xa0000
#define VGA_MEM_SIZE 0x20000
```

**范围**: `0xa0000 - 0xbffff` (128 KB)

**用途**:
- VGA 文本模式
- VGA 图形模式
- VBE (VESA BIOS Extensions)

### 3.3 缓存机制

**缓存状态**:

```103:118:xen/xen/arch/x86/hvm/stdvga.c
static void stdvga_try_cache_enable(struct hvm_hw_stdvga *s)
{
    /*
     * Caching mode can only be enabled if the the cache has
     * never been used before. As soon as it is disabled, it will
     * become out-of-sync with the VGA device model and since no
     * mechanism exists to acquire current VRAM state from the
     * device model, re-enabling it would lead to stale data being
     * seen by the guest.
     */
    if ( s->cache != STDVGA_CACHE_UNINITIALIZED )
        return;

    gdprintk(XENLOG_INFO, "entering caching mode\n");
    s->cache = STDVGA_CACHE_ENABLED;
}
```

**缓存策略**:
- 一旦禁用，无法重新启用（避免数据不一致）
- 缓存 VGA 寄存器状态
- 减少对 QEMU 的调用

## 四、虚拟显示接口 (displif)

### 4.1 概述

**displif** 是 Xen 提供的统一显示设备 I/O 接口，用于虚拟显示。

**特点**:
- ✅ 多个动态分配的 framebuffer
- ✅ 任意大小的缓冲区
- ✅ 前后端缓冲区分配
- ✅ 多显示器支持
- ✅ 与 fbif 兼容

**协议版本**: 2

**参考**: `xen/xen/include/public/io/displif.h`

### 4.2 功能特性

**主要功能**:

```28:48:xen/xen/include/public/io/displif.h
 * This protocol aims to provide a unified protocol which fits more
 * sophisticated use-cases than a framebuffer device can handle. At the
 * moment basic functionality is supported with the intention to be extended:
 *  o multiple dynamically allocated/destroyed framebuffers
 *  o buffers of arbitrary sizes
 *  o buffer allocation at either back or front end
 *  o better configuration options including multiple display support
 *
 * Note: existing fbif can be used together with displif running at the
 * same time, e.g. on Linux one provides framebuffer and another DRM/KMS
 *
 * Note: display resolution (XenStore's "resolution" property) defines
 * visible area of the virtual display. At the same time resolution of
 * the display and frame buffers may differ: buffers can be smaller, equal
 * or bigger than the visible area. This is to enable use-cases, where backend
 * may do some post-processing of the display and frame buffers supplied,
 * e.g. those buffers can be just a part of the final composition.
```

**未来扩展**:
- 显示/连接器克隆
- 其他对象的分配
- 平面/覆盖支持
- 缩放支持
- 旋转支持

## 五、配置和使用

### 5.1 基本配置

**xl.cfg 配置示例**:

```bash
# GPU Passthrough 配置
name = "gpu-vm"
builder = "hvm"
memory = 4096
vcpus = 4

# 启用 gfx_passthru
gfx_passthru = 1
# 或指定类型
# gfx_passthru = "igd"

# 指定 PCI 设备（GPU）
pci = [ '01:00.0' ]

# 其他配置
disk = [ 'phy:/dev/sda1,xvda,w' ]
vif = [ 'bridge=xenbr0' ]
```

### 5.2 设备准备

**步骤**:

1. **检查设备**:
```bash
lspci | grep -i vga
# 或
lspci | grep -i nvidia
```

2. **隐藏设备**:
```bash
# 方法 1: 动态隐藏
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/nvidia/unbind
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/pciback/new_slot
echo -n 0000:01:00.0 > /sys/bus/pci/drivers/pciback/bind

# 方法 2: 启动参数
# 在 grub 中添加: pciback.hide=(01:00.0)
```

3. **启动 Guest**:
```bash
xl create gpu-vm.cfg
```

### 5.3 验证

**在 Guest 中验证**:

```bash
# 检查 PCI 设备
lspci | grep -i vga

# 检查驱动
lsmod | grep nvidia

# 检查 X 服务器
glxinfo | grep -i opengl
```

## 六、限制和注意事项

### 6.1 硬件限制

**要求**:
- ✅ **IOMMU 支持**: Intel VT-d 或 AMD-Vi
- ✅ **设备隔离**: 设备必须从 Domain 0 隐藏
- ⚠️ **不支持迁移**: PCI Passthrough 设备不支持迁移
- ⚠️ **不支持共享**: GPU 一次只能分配给一个 VM

### 6.2 软件限制

**不兼容的功能**:
- ❌ 迁移 (Migration)
- ❌ 按需填充 (Populate-on-demand)
- ❌ altp2m
- ❌ 内省 (Introspection)
- ❌ 内存共享 (Memory Sharing)
- ❌ 内存分页 (Memory Paging)

**参考**: `xen/SUPPORT.md:800-816`

```800:816:xen/SUPPORT.md
### x86/PCI Device Passthrough

    Status, x86 PV: Supported, with caveats
    Status, x86 HVM: Supported, with caveats

Only systems using IOMMUs are supported.

Not compatible with migration, populate-on-demand, altp2m,
introspection, memory sharing, or memory paging.

Because of hardware limitations
(affecting any operating system or hypervisor),
it is generally not safe to use this feature
to expose a physical device to completely untrusted guests.
However, this feature can still confer significant security benefit
when used to remove drivers and backends from domain 0
(i.e., Driver Domains).
```

### 6.3 设备特定问题

**某些设备不适合 HVM Passthrough**:

**问题设备特征**:
- 设备有内部资源（如私有内存）
- 资源映射到内存地址空间（BAR）
- 驱动提交包含指向内部资源缓冲区的指针的命令
- 设备解码指针（地址）并访问缓冲区

**原因**:
- 在 HVM Domain 中，BAR 被虚拟化
- 主机 BAR 值和 Guest BAR 值不同
- 设备视图和驱动视图的地址不同
- 设备无法访问驱动指定的缓冲区

**参考**: `xen/docs/misc/vtd.txt:220-236`

### 6.4 AMD/ATI 显卡

**特殊说明**:

```1218:1223:xen/docs/man/xl.cfg.5.pod.in
Note that some graphics cards (AMD/ATI cards, for example) do not
necessarily require the B<gfx_passthru> option, so you can use the normal Xen
PCI passthrough to assign the graphics card as a secondary graphics
card to the VM. The QEMU-emulated graphics card remains the primary
graphics card, and VNC output is available from the QEMU-emulated
primary adapter.
```

**特点**:
- 可以作为**次要显卡**使用
- 不需要 `gfx_passthru` 选项
- QEMU 模拟的显卡仍然是主显卡
- VNC 输出仍然可用

## 七、代码结构

### 7.1 关键文件

```
xen/tools/libs/light/
├── libxl_pci.c          # PCI 设备管理
│   ├── libxl__grant_vga_iomem_permission()  # VGA IOMEM 权限
│   └── libxl__is_igd_vga_passthru()         # IGD 检测
├── libxl_dm.c           # Device Model 配置
│   ├── libxl__detect_gfx_passthru_kind()    # GPU 类型检测
│   └── libxl__build_dm_args()               # QEMU 参数构建
└── libxl_types.idl      # 类型定义
    └── libxl_gfx_passthru_kind              # GPU Passthrough 类型

xen/xen/drivers/passthrough/
├── pci.c                # PCI Passthrough 核心
│   ├── assign_device()  # 设备分配
│   ├── iommu_add_device()  # IOMMU 设备添加
│   └── iommu_enable_device()  # IOMMU 设备启用
└── vtd/                 # Intel VT-d 实现
    └── iommu.c

xen/xen/arch/x86/hvm/
└── stdvga.c             # 标准 VGA 模拟

xen/xen/include/public/io/
└── displif.h            # 虚拟显示接口定义
```

### 7.2 数据流

```
1. 配置解析 (xl_parse.c)
   ↓
2. 类型检测 (libxl_dm.c)
   ├─ libxl__detect_gfx_passthru_kind()
   └─ libxl__is_igd_vga_passthru()
   ↓
3. VGA IOMEM 权限 (libxl_pci.c)
   └─ libxl__grant_vga_iomem_permission()
   ↓
4. PCI 设备分配 (pci.c)
   ├─ assign_device()
   └─ iommu_add_device()
   ↓
5. Device Model 启动 (libxl_dm.c)
   └─ 传递 -gfx_passthru 或 igd-passthru=on
   ↓
6. Guest 使用物理 GPU
```

## 八、最佳实践

### 8.1 设备选择

**推荐**:
- ✅ 使用经过测试的 GPU（参考 Xen Wiki）
- ✅ 优先使用独立显卡（而非集成显卡）
- ✅ 确保 IOMMU 支持

**避免**:
- ❌ 使用未测试的 GPU
- ❌ 在无 IOMMU 的系统上使用
- ❌ 尝试共享 GPU

### 8.2 配置建议

**内存**:
- 为 Guest 分配足够的视频内存
- 考虑 GPU 的显存需求

**CPU**:
- 分配足够的 vCPU
- 考虑 GPU 计算需求

**网络**:
- 如果需要远程访问，使用 VNC/SPICE（非 gfx_passthru）
- 或使用 displif 虚拟显示

### 8.3 故障排查

**常见问题**:

1. **设备未显示**:
   - 检查设备是否从 Domain 0 隐藏
   - 检查 IOMMU 是否启用
   - 检查 PCI 配置是否正确

2. **驱动加载失败**:
   - 检查 Guest 内核是否支持设备
   - 检查 VBIOS 是否正确复制
   - 检查 BAR 映射是否正确

3. **性能问题**:
   - 检查 IOMMU 映射
   - 检查中断路由
   - 检查 CPU 亲和性

## 九、总结

### 9.1 核心要点

1. **GPU Passthrough**: 通过 `gfx_passthru` 选项启用，需要 IOMMU 支持
2. **设备隔离**: 设备必须从 Domain 0 隐藏
3. **类型检测**: 自动检测或手动指定 GPU 类型（IGD/Default）
4. **VGA 内存**: 需要授予 VGA 内存范围权限
5. **限制**: 不支持迁移、共享等功能

### 9.2 适用场景

**适合**:
- 高性能图形应用
- GPU 计算任务
- 游戏虚拟机
- 专业图形工作站

**不适合**:
- 需要迁移的场景
- 需要 GPU 共享的场景
- 无 IOMMU 的系统

### 9.3 技术路线

**选择建议**:
- **高性能需求**: GPU Passthrough (gfx_passthru)
- **兼容性优先**: 标准 PCI Passthrough
- **虚拟化优先**: displif 虚拟显示
- **简单场景**: QEMU 模拟 VGA

## 十、参考

- `xen/docs/man/xl.cfg.5.pod.in` - 配置文档
- `xen/docs/misc/vtd.txt` - VT-d 配置指南
- `xen/SUPPORT.md` - 支持状态
- [Xen VGA Passthrough Wiki](https://wiki.xenproject.org/wiki/XenVGAPassthrough)
- [Xen VGA Passthrough Tested Adapters](https://wiki.xenproject.org/wiki/XenVGAPassthroughTestedAdapters)
- [PCI Passthrough 文档](./domainU-configuration-guide.md) - Domain U 配置指南
