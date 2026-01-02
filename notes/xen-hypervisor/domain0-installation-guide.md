# Domain 0 获取和安装指南

**日期**: 2026-01-01
**相关文件**:
- `xen/README` - Xen 构建说明
- `xen/INSTALL` - 安装说明

## 概述

Domain 0 的内核不在 Xen 源码中，需要单独获取和安装。本文档提供多种获取和安装 Domain 0 的方法。

**重要提示**:
1. Linux 内核从 3.0 版本开始已**内置支持 Xen 半虚拟化**
2. **Domain 0 可以是任何 Linux 发行版**（Debian、Ubuntu、CentOS、Fedora 等）
3. 只需要：
   - 内核支持 Xen 半虚拟化（大多数发行版已提供）
   - 安装 Xen 用户态工具（xl、xenstore 等）

**详细说明**: 参见 [Domain 0 发行版选择](./domain0-distribution-choice.md)

## 一、方法一：使用发行版提供的预编译内核（推荐）

这是最简单的方法，适用于大多数用户。

### 1.1 Debian/Ubuntu

#### 安装 Xen 支持的内核

```bash
# Debian
sudo apt-get update
sudo apt-get install linux-image-xen-amd64 linux-headers-xen-amd64

# Ubuntu (如果可用)
sudo apt-get install linux-image-generic-xen linux-headers-generic-xen
```

#### 验证内核支持

```bash
# 检查内核配置
zcat /proc/config.gz | grep -i xen

# 应该看到类似输出：
# CONFIG_XEN=y
# CONFIG_XEN_DOM0=y
# CONFIG_XEN_BLKDEV_FRONTEND=y
# CONFIG_XEN_NETDEV_FRONTEND=y
```

### 1.2 CentOS/RHEL/Fedora

```bash
# CentOS/RHEL 7/8
sudo yum install kernel-xen kernel-xen-devel

# Fedora
sudo dnf install kernel-xen kernel-xen-devel
```

### 1.3 其他发行版

参考发行版的文档或包管理器搜索 "xen" 相关的内核包。

## 二、方法二：从源码编译 Linux 内核（高级）

如果需要自定义内核配置或使用最新内核，可以从源码编译。

### 2.1 获取 Linux 内核源码

```bash
# 从 kernel.org 下载
wget https://www.kernel.org/pub/linux/kernel/v6.x/linux-6.x.x.tar.xz
tar -xf linux-6.x.x.tar.xz
cd linux-6.x.x

# 或使用 git
git clone https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
cd linux
```

### 2.2 配置内核支持 Xen

#### 方法 A: 使用现有配置作为基础

```bash
# 使用当前运行内核的配置
cp /boot/config-$(uname -r) .config

# 或使用发行版默认配置
make defconfig
```

#### 方法 B: 手动配置

```bash
make menuconfig
```

**必须启用的配置选项**:

```
Processor type and features
  └─> Linux guest support
      └─> [*] Enable paravirtualization code
          └─> [*] Xen guest support
              └─> [*] Support for running as a Xen guest
                  └─> [*] Support for running as a Xen Dom0

Device Drivers
  └─> [*] Block devices
      └─> [*] Xen block device support
  └─> [*] Network device support
      └─> [*] Xen network device frontend
  └─> [*] XEN Platform support
      └─> [*] Xen platform PCI driver
      └─> [*] Xen balloon driver
      └─> [*] Xen /dev/xen/evtchn device
      └─> [*] Xen filesystem
      └─> [*] Xen grant device driver
      └─> [*] Xen platform-pci driver
```

**关键配置选项**:
- `CONFIG_XEN=y` - 启用 Xen 支持
- `CONFIG_XEN_DOM0=y` - 支持作为 Domain 0 运行
- `CONFIG_XEN_BLKDEV_FRONTEND=y` - 块设备前端驱动
- `CONFIG_XEN_NETDEV_FRONTEND=y` - 网络设备前端驱动
- `CONFIG_XEN_PCIDEV_FRONTEND=y` - PCI 设备前端驱动

### 2.3 编译内核

```bash
# 设置内核版本（可选）
make LOCALVERSION=-xen

# 编译内核
make -j$(nproc)

# 编译模块
make modules -j$(nproc)

# 安装模块
sudo make modules_install

# 安装内核
sudo make install
```

### 2.4 更新引导配置

```bash
# 更新 GRUB（大多数发行版）
sudo update-grub

# 或手动编辑 GRUB 配置
sudo nano /etc/default/grub
```

**GRUB 配置示例**:

```bash
GRUB_CMDLINE_XEN_DEFAULT="dom0_mem=512M"
GRUB_CMDLINE_LINUX_DEFAULT=""
```

## 三、方法三：使用 Xen 项目推荐的内核

根据 `xen/README` 的说明：

> If no suitable kernel is available from your OS distributor then refer to
> https://wiki.xen.org/wiki/XenDom0Kernels for suggestions for
> suitable kernels to use.

### 3.1 查看推荐内核列表

访问: https://wiki.xen.org/wiki/XenDom0Kernels

### 3.2 编译 Dom0 内核的详细指南

参考: https://wiki.xen.org/wiki/XenParavirtOps

## 四、安装和配置 Domain 0

### 4.1 安装 Xen 工具

**重要**: Domain 0 需要安装 Xen 的用户态管理工具。这些工具是独立的应用程序，可以安装到任何 Linux 发行版。

```bash
# Debian/Ubuntu
sudo apt-get install xen-tools xen-utils

# CentOS/RHEL
sudo yum install xen-tools

# Fedora
sudo dnf install xen-tools

# 或从源码编译（在 xen 目录）
make tools -j$(nproc)
sudo make install-tools
```

**主要工具**:
- `xl` - Xen 管理工具（必需）
- `xenstore` / `oxenstored` - 配置数据库（必需）
- `xenconsoled` - 控制台守护进程（必需）
- `libxl` - Xen 管理库（必需）

**详细说明**: 参见 [Domain 0 发行版选择](./domain0-distribution-choice.md) 中的"用户态工具要求"部分

### 4.2 配置 GRUB 引导 Xen

#### 自动配置（推荐）

如果使用发行版包管理器安装，通常会自动配置 GRUB。

#### 手动配置

编辑 `/etc/default/grub`:

```bash
# 设置 Xen 为默认引导项
GRUB_DEFAULT="Xen 4.x.x"

# Xen 命令行参数
GRUB_CMDLINE_XEN_DEFAULT="dom0_mem=512M,max:512M dom0_max_vcpus=2"

# Linux 内核参数（Domain 0）
GRUB_CMDLINE_LINUX_DEFAULT=""
```

更新 GRUB:

```bash
sudo update-grub
```

### 4.3 配置 Xen 服务

#### Systemd（现代发行版）

```bash
# 启用 Xen 服务
sudo systemctl enable xen-qemu-dom0-disk-backend.service
sudo systemctl enable xen-init-dom0.service
sudo systemctl enable xenconsoled.service

# 可选服务
sudo systemctl enable xendomains.service
sudo systemctl enable xen-watchdog.service
```

#### SysV init（传统发行版）

```bash
# 启用 Xen 服务
sudo update-rc.d xencommons defaults
sudo update-rc.d xendomains defaults
```

### 4.4 验证安装

#### 检查 Xen 是否运行

```bash
# 检查 Xen 版本
sudo xl info

# 检查 Domain 列表
sudo xl list

# 应该看到 Domain 0
# Name                                        ID   Mem VCPUs      State   Time(s)
# Domain-0                                     0  1024     2     r-----     123.4
```

#### 检查内核模块

```bash
# 检查 Xen 相关模块
lsmod | grep xen

# 应该看到类似输出：
# xen_blkfront
# xen_netfront
# xenfs
# xen_gntalloc
# xen_gntdev
# xen_evtchn
```

## 五、常见问题和故障排除

### 5.1 Domain 0 无法启动

**问题**: 系统无法引导到 Domain 0

**解决方案**:
1. 检查 GRUB 配置是否正确
2. 检查内核是否支持 Xen
3. 检查 Xen 二进制文件是否存在: `ls -l /boot/xen-*`
4. 查看引导日志: `dmesg | grep -i xen`

### 5.2 内核模块未加载

**问题**: Xen 前端驱动未加载

**解决方案**:
```bash
# 手动加载模块
sudo modprobe xen_blkfront
sudo modprobe xen_netfront
sudo modprobe xenfs

# 添加到 /etc/modules 自动加载
echo "xen_blkfront" | sudo tee -a /etc/modules
echo "xen_netfront" | sudo tee -a /etc/modules
echo "xenfs" | sudo tee -a /etc/modules
```

### 5.3 内存不足

**问题**: Domain 0 内存不足

**解决方案**:
1. 在 GRUB 中限制 Domain 0 内存:
   ```bash
   GRUB_CMDLINE_XEN_DEFAULT="dom0_mem=512M,max:1G"
   ```
2. 重启系统

### 5.4 网络无法工作

**问题**: Domain 0 网络接口未出现

**解决方案**:
```bash
# 检查网络前端驱动
lsmod | grep xen_netfront

# 检查网络接口
ip link show

# 如果缺少接口，检查 xenstore
sudo xenstore-ls
```

## 六、不同架构的注意事项

### 6.1 x86_64

- 支持 PV (Paravirtualized) 和 HVM (Hardware Virtual Machine) 模式
- Domain 0 通常使用 PV 模式
- 需要启用相应的内核配置选项

### 6.2 ARM

- 主要使用 HVM 模式（硬件辅助虚拟化）
- 需要支持 ARM 虚拟化扩展的内核
- 使用 Device Tree 描述硬件

### 6.3 其他架构

参考架构特定的文档和配置要求。

## 七、最佳实践

### 7.1 内核版本选择

- **生产环境**: 使用发行版提供的稳定版本
- **开发/测试**: 可以使用较新版本，但需要充分测试
- **避免**: 使用过旧或过新的内核版本

### 7.2 内存配置

- **最小**: 512MB（仅用于测试）
- **推荐**: 1-2GB（小型部署）
- **生产**: 根据实际需求配置（通常 2-4GB）

### 7.3 安全考虑

- 定期更新内核和 Xen
- 使用发行版提供的安全更新
- 限制 Domain 0 的权限（如果可能）

## 八、参考资源

### 8.1 官方文档

- [Xen Project Wiki - Dom0 Kernels](https://wiki.xen.org/wiki/XenDom0Kernels)
- [Xen Project Wiki - Xen ParavirtOps](https://wiki.xen.org/wiki/XenParavirtOps)
- [Xen Project Wiki - Compiling Xen from Source](https://wiki.xenproject.org/wiki/Compiling_Xen_From_Source)

### 8.2 发行版特定文档

- [Debian Wiki - Xen](https://wiki.debian.org/Xen)
- [Ubuntu Wiki - Xen](https://wiki.ubuntu.com/Xen)
- [CentOS Wiki - Xen](https://wiki.centos.org/HowTos/Xen)

### 8.3 相关文件

- `xen/README` - Xen 构建说明
- `xen/INSTALL` - 安装说明
- [Domain 0 的本质](./domain0-essence.md) - Domain 0 概念说明
- [Xen 虚拟化模式详解](./xen-virtualization-modes.md) - 虚拟化模式说明

## 九、总结

### 9.1 推荐流程

1. **首选**: 使用发行版提供的预编译内核
2. **备选**: 从源码编译（如果需要自定义配置）
3. **配置**: 设置 GRUB 和 Xen 服务
4. **验证**: 确认 Domain 0 正常运行

### 9.2 关键要点

- Domain 0 内核不在 Xen 源码中
- 需要支持 Xen 的内核（不能使用任意内核）
- 推荐使用发行版提供的预编译版本
- 从源码编译需要正确配置 Xen 相关选项
