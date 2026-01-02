# Domain 0 的本质

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/common/domain.c` - Domain 创建
- `xen/xen/arch/*/domain_build.c` - Domain 构建
- `xen/docs/admin-guide/introduction.rst` - Xen 官方文档

## 核心观点

**Domain 0 就是一个普通的虚拟机，只是它被赋予了特权，可以调用 Xen 的管理接口。**

**重要**: Domain 0 可以是**任何 Linux 发行版**（Debian、Ubuntu、CentOS、Fedora、Arch 等），只要：
1. 内核支持 Xen 半虚拟化（Linux 3.0+ 已内置支持）
2. 安装了 Xen 的用户态工具（xl、xenstore 等）

## 一、Domain 0 不在 Xen 源码中

### 1.1 Xen 源码只包含 Hypervisor

Xen 源码树（`xen/xen/`）只包含：
- Hypervisor 本身
- 创建和管理 Domain 的代码
- **不包含** Domain 0 的内核源码

### 1.2 Domain 0 需要单独获取

Domain 0 的内核需要：
- 从 Linux 内核源码编译（需要 Xen 支持）
- 或使用发行版提供的预编译内核（如 `linux-image-xen-amd64`）

**参考**: `xen/README` 第 91-97 行明确说明需要单独获取 Domain 0 内核。

## 二、Domain 0 就是一个虚拟机

### 2.1 官方文档的说明

根据 `xen/docs/admin-guide/introduction.rst`:

```13:21:xen/docs/admin-guide/introduction.rst
When Xen boots, dom0 is automatically started as well.  Dom0 is a virtual
machine which, by default, is granted full permissions [1]_.  A typical setup
might be:

.. image:: xen-overview.drawio.svg

Dom0 takes the role of :term:`control domain`, responsible for creating and
managing other virtual machines, and the role of :term:`hardware domain`,
responsible for hardware and marshalling guest I/O.
```

**关键点**: "Dom0 is a virtual machine" - Domain 0 就是一个虚拟机。

### 2.2 官方文档的澄清

文档的脚注特别强调：

```33:35:xen/docs/admin-guide/introduction.rst
.. [1] A common misconception with Xen's architecture is that dom0 is somehow
       different to other guests.  The choice of id 0 is not an accident, and
       follows in UNIX heritage.
```

**重要澄清**: 这是一个常见的误解 - Domain 0 在架构上与其他 Guest 没有本质区别，只是 ID 为 0（遵循 UNIX 传统）。

### 2.3 Domain 0 的创建过程

Domain 0 的创建与其他 Domain 使用**相同的函数**：

```3919:3919:xen/xen/arch/arm/domain_build.c
    dom0 = domain_create(0, &dom0_cfg, CDF_privileged | CDF_directmap);
```

**关键区别**: 只是创建时使用了 `CDF_privileged` 标志。

## 三、Domain 0 的特权

### 3.1 特权标志

Domain 0 通过 `CDF_privileged` 标志获得特权：

```40:40:xen/xen/include/xen/domain.h
#define CDF_privileged           (1U << 0)
```

```614:614:xen/xen/common/domain.c
    d->is_privileged = flags & CDF_privileged;
```

### 3.2 特权检查

系统通过 `is_control_domain()` 检查是否为控制域：

```1083:1088:xen/xen/include/xen/sched.h
static always_inline bool is_control_domain(const struct domain *d)
{
    if ( !d )
        return false;
    return evaluate_nospec(d->is_privileged);
}
```

### 3.3 Domain 0 可以做什么

Domain 0 的特权包括：
1. **创建和管理其他 Domain**: 通过 `XEN_DOMCTL_*` hypercall
2. **访问硬件**: 作为硬件域（hardware domain）直接访问物理设备
3. **调用管理接口**: 访问 Xen 的管理功能（sysctl、domctl 等）
4. **控制资源分配**: 分配 CPU、内存等资源给其他 Domain

**参考**: `xen/tools/flask/policy/modules/dom0.te` 列出了 Domain 0 的所有权限。

## 四、Domain 0 需要什么内核？

### 4.1 Linux 内核支持 Xen 半虚拟化

**重要**: Linux 内核从 **3.0 版本开始已内置 Xen 半虚拟化支持**！

虽然 Domain 0 是普通虚拟机，但它需要：
- **支持 Xen 的 hypercall 接口**: ✅ Linux 内核已内置支持
- **支持 Xen 的虚拟化模式**:
  - **PV (Paravirtualized)**: ✅ Linux 内核完全支持，Domain 0 的推荐模式
  - **PVH (Paravirtualized Hardware)**: ✅ Linux 内核支持，但 Domain 0 支持为实验性
  - **HVM (Hardware Virtual Machine)**: Domain 0 不使用此模式
- **Xen 设备驱动**: 用于访问虚拟化后的设备（前端驱动）

**详细说明**: 参见 [Xen 虚拟化模式详解](./xen-virtualization-modes.md)

### 4.2 常见的内核选择

1. **Linux 内核**（最常见）:
   - ✅ **Linux 内核 3.0+ 已内置 Xen 半虚拟化支持**
   - 只需要启用 `CONFIG_XEN_DOM0` 等配置选项
   - 大多数发行版提供的 Xen 内核已正确配置
   - 支持 PV 和 PVH 模式

2. **其他支持 Xen 的内核**:
   - NetBSD
   - FreeBSD
   - 理论上任何支持 Xen hypercall 的内核都可以

### 4.3 内核配置要求

Domain 0 内核需要：
- Xen 前端驱动（网络、块设备等）
- Xen 管理接口支持
- 适当的虚拟化模式支持（PV 或 HVM）

## 五、Domain 0 vs 普通 Domain

### 5.1 相同点

| 特性 | Domain 0 | 普通 Domain |
|------|----------|-------------|
| 是虚拟机 | ✅ | ✅ |
| 运行在 Xen 上 | ✅ | ✅ |
| 有 VCPU | ✅ | ✅ |
| 有内存 | ✅ | ✅ |
| 可以运行应用 | ✅ | ✅ |

### 5.2 不同点

| 特性 | Domain 0 | 普通 Domain |
|------|----------|-------------|
| Domain ID | 0 | 1, 2, 3... |
| 特权标志 | `CDF_privileged` | 无 |
| 可以创建其他 Domain | ✅ | ❌ |
| 可以直接访问硬件 | ✅ | ❌（通常） |
| 可以调用管理接口 | ✅ | ❌ |
| 内存映射 | `CDF_directmap`（直接映射） | 虚拟映射 |

### 5.3 代码中的体现

```3919:3919:xen/xen/arch/arm/domain_build.c
    dom0 = domain_create(0, &dom0_cfg, CDF_privileged | CDF_directmap);
```

**关键标志**:
- `CDF_privileged`: 赋予特权
- `CDF_directmap`: 直接内存映射（ARM 架构）

## 六、Domain 0 的角色

### 6.1 控制域 (Control Domain)

Domain 0 负责：
- 创建、销毁、暂停、恢复其他 Domain（Domain U）
- 管理 Domain 的生命周期
- 配置 Domain 的资源

**详细说明**: 参见 [Domain U 配置和维护指南](./domainU-configuration-guide.md)

### 6.2 硬件域 (Hardware Domain)

Domain 0 负责：
- 直接访问物理硬件设备
- 为其他 Domain 提供设备驱动后端
- 处理 I/O 请求的转发

### 6.3 管理域 (Management Domain)

Domain 0 运行管理工具（用户态应用程序）：

**必需工具**:
- **`xl`** - Xen 管理工具（创建、管理虚拟机）
- **`xenstore`** - 配置数据库（域间通信）
- **`xenconsoled`** - 控制台守护进程
- **`libxl`** - Xen 管理库（xl 工具的后端）

**可选工具**:
- `xentop` - 监控工具
- `xentrace` - 跟踪工具
- `xenmon` - 性能监控
- `xenpm` - 电源管理
- `xenwatchdogd` - 看门狗守护进程

**后端驱动**（内核模块）:
- `xen-blkback` - 块设备后端
- `xen-netback` - 网络设备后端
- `xen-pciback` - PCI 设备后端

这些工具都在 `xen/tools/` 目录中，需要单独编译和安装。

## 七、总结

### 7.1 核心要点

1. ✅ **Domain 0 不在 Xen 源码中** - 需要单独获取内核
2. ✅ **Domain 0 就是普通虚拟机** - 架构上没有本质区别
3. ⚠️ **需要支持 Xen 的内核** - 不能运行任意内核，需要 Xen 支持
4. ✅ **通过特权标志区分** - `CDF_privileged` 标志赋予特权
5. ✅ **可以调用 Xen 服务** - 通过 hypercall 接口管理其他 Domain

### 7.2 类比理解

可以这样理解：
- **Xen Hypervisor** = 操作系统内核
- **Domain 0** = root 用户（有特权）
- **其他 Domain** = 普通用户（无特权）

Domain 0 就像 UNIX 系统中的 root 用户，有特殊权限，但本质上还是用户。

### 7.3 设计哲学

Xen 的设计哲学：
- **最小化 Hypervisor**: Xen 只做必要的虚拟化工作
- **将复杂性移到用户空间**: 设备驱动、管理功能在 Domain 0 中
- **Domain 0 是第一个用户**: 不是 Hypervisor 的一部分

### 7.4 Domain 0 可以是任何 Linux 发行版

**重要**: Domain 0 可以是**任何 Linux 发行版**（Debian、Ubuntu、CentOS、Fedora、Arch 等），只要：
1. 内核支持 Xen 半虚拟化（Linux 3.0+ 已内置支持）
2. 安装了 Xen 的用户态管理工具（xl、xenstore 等）

**详细说明**: 参见 [Domain 0 发行版选择](./domain0-distribution-choice.md)

## 八、如何获取和安装 Domain 0

Domain 0 需要单独获取和安装，详细步骤请参考：

**[Domain 0 获取和安装指南](./domain0-installation-guide.md)**

该指南包含：
- 使用发行版预编译内核（推荐）
- 从源码编译 Linux 内核
- 配置和验证安装
- 常见问题排除

## 九、参考

- `xen/docs/admin-guide/introduction.rst` - Xen 官方介绍
- `xen/xen/common/domain.c` - Domain 创建代码
- `xen/xen/include/xen/domain.h` - Domain 定义
- [Xen Project Wiki - Domain 0](https://wiki.xenproject.org/wiki/Xen_Project_Software_Overview#Domain_0)
- [Domain 0 获取和安装指南](./domain0-installation-guide.md) - 安装步骤
- [Domain U 配置和维护指南](./domainU-configuration-guide.md) - Domain U 管理
