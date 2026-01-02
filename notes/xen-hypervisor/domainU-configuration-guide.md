# Domain U 配置和维护指南

**日期**: 2026-01-01
**相关文件**:
- `xen/tools/xl/` - xl 管理工具
- `xen/tools/examples/` - 配置示例
- `xen/tools/libs/light/` - libxl 库

## 概述

Domain U（也称为 DomU 或 Guest Domain）是 Xen 系统中的普通虚拟机。与 Domain 0 不同，Domain U 没有特权，不能创建或管理其他 Domain，但可以运行任何支持的操作系统。

**核心观点**: Domain U 就是普通的虚拟机，可以是任何操作系统（Linux、Windows、BSD 等），只要支持相应的虚拟化模式。

## 一、Domain U 的概念

### 1.1 什么是 Domain U？

**Domain U** 是 Xen 术语，指：
- **U** = Unprivileged（非特权）
- 普通的虚拟机
- 由 Domain 0 创建和管理
- 没有管理其他 Domain 的权限

### 1.2 Domain U vs Domain 0

| 特性 | Domain 0 | Domain U |
|------|----------|----------|
| **特权** | ✅ 有特权 | ❌ 无特权 |
| **Domain ID** | 0 | 1, 2, 3... |
| **可以创建其他 Domain** | ✅ | ❌ |
| **可以访问硬件** | ✅ 直接访问 | ❌ 通过虚拟设备 |
| **运行的操作系统** | Linux（通常） | 任何支持的操作系统 |
| **虚拟化模式** | PV 或 PVH | PV、PVH 或 HVM |

### 1.3 Domain U 可以运行什么？

**Linux**:
- ✅ 任何 Linux 发行版（支持 PV 或 PVH）
- ✅ 使用 PV 模式（推荐）
- ✅ 使用 PVH 模式（性能更好）

**Windows**:
- ✅ 使用 HVM 模式
- ✅ 需要硬件虚拟化支持（VT-x/AMD-V）

**其他操作系统**:
- ✅ NetBSD、FreeBSD（支持 PV）
- ✅ 任何支持 HVM 的操作系统

## 二、配置文件格式

### 2.1 配置文件位置

Domain U 的配置文件通常位于：
```
/etc/xen/DOMAIN_NAME.cfg
```

例如：`/etc/xen/myvm.cfg`

### 2.2 配置文件语法

配置文件使用 `KEY=VALUE` 格式：

```bash
# 注释以 # 开头
name = "myvm"
memory = 512
vcpus = 2
```

### 2.3 必需配置项

**所有 Domain U 都需要**:
- `name` - Domain 名称（必需）

**根据虚拟化模式不同，还需要**:
- **PV/PVH**: `kernel` - 内核镜像路径
- **HVM**: `type = "hvm"` - 指定 HVM 模式

## 三、PV 模式配置（半虚拟化）

### 3.1 基本配置

**位置**: `xen/tools/examples/xlexample.pvlinux`

```1:44:xen/tools/examples/xlexample.pvlinux
# =====================================================================
# Example PV Linux guest configuration
# =====================================================================
#
# This is a fairly minimal example of what is required for a
# Paravirtualised Linux guest. For a more complete guide see xl.cfg(5)

# Guest name
name = "example.pvlinux"

# 128-bit UUID for the domain as a hexadecimal number.
# Use "uuidgen" to generate one if required.
# The default behavior is to generate a new UUID each time the guest is started.
#uuid = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"

# Kernel image to boot
kernel = "/boot/vmlinuz"

# Ramdisk (optional)
#ramdisk = "/boot/initrd.gz"

# Kernel command line options
extra = "root=/dev/xvda1"

# Initial memory allocation (MB)
memory = 128

# Maximum memory (MB)
# If this is greater than `memory' then the slack will start ballooned
# (this assumes guest kernel support for ballooning)
#maxmem = 512

# Number of VCPUS
vcpus = 2

# Network devices
# A list of 'vifspec' entries as described in
# docs/misc/xl-network-configuration.markdown
vif = [ '' ]

# Disk Devices
# A list of `diskspec' entries as described in
# docs/misc/xl-disk-configuration.txt
disk = [ '/dev/vg/guest-volume,raw,xvda,rw' ]
```

### 3.2 PV 模式配置项说明

**基本配置**:
- `name` - Domain 名称
- `kernel` - 内核镜像路径（必需）
- `ramdisk` - 初始 RAM 磁盘（可选）
- `extra` - 内核命令行参数

**资源分配**:
- `memory` - 初始内存（MB）
- `maxmem` - 最大内存（MB，支持内存气球）
- `vcpus` - VCPU 数量

**设备配置**:
- `disk` - 磁盘设备列表
- `vif` - 网络接口列表

### 3.3 完整 PV 配置示例

```bash
# /etc/xen/my-pv-vm.cfg

# 基本配置
name = "my-pv-vm"
kernel = "/boot/vmlinuz-5.10.0-xen"
ramdisk = "/boot/initrd.img-5.10.0-xen"
extra = "root=/dev/xvda1 ro console=hvc0"

# 资源分配
memory = 1024
maxmem = 2048
vcpus = 2

# CPU 亲和性（可选）
cpus = "0-1"

# 磁盘配置
disk = [
    '/dev/vg/myvm-disk,raw,xvda,rw',
    '/dev/vg/myvm-swap,raw,xvdb,rw'
]

# 网络配置
vif = [
    'bridge=xenbr0',
    'mac=00:16:3e:XX:XX:XX'
]

# 启动行为
on_poweroff = "destroy"
on_reboot = "restart"
on_crash = "restart"
```

## 四、PVH 模式配置（新一代半虚拟化）

### 4.1 基本配置

**位置**: `xen/tools/examples/xlexample.pvhlinux`

```1:42:xen/tools/examples/xlexample.pvhlinux
# =====================================================================
# Example PVH Linux guest configuration
# =====================================================================
#
# This is a fairly minimal example of what is required for a
# PVH Linux guest. For a more complete guide see xl.cfg(5)

# This configures a PVH rather than PV guest
type = "pvh"

# Guest name
name = "example.pvhlinux"

# 128-bit UUID for the domain as a hexadecimal number.
# Use "uuidgen" to generate one if required.
# The default behavior is to generate a new UUID each time the guest is started.
#uuid = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"

# Kernel image to boot
kernel = "/boot/vmlinuz"

# Ramdisk (optional)
#ramdisk = "/boot/initrd.gz"

# Kernel command line options
extra = "root=/dev/xvda1"

# Initial memory allocation (MB)
memory = 512

# Number of VCPUS
vcpus = 4

# Network devices
# A list of 'vifspec' entries as described in
# docs/misc/xl-network-configuration.markdown
vif = [ '' ]

# Disk Devices
# A list of `diskspec' entries as described in
# docs/misc/xl-disk-configuration.txt
disk = [ '/dev/zvol/tank/guest-volume,raw,xvda,rw' ]
```

### 4.2 PVH 模式特点

- ✅ **需要硬件虚拟化支持**（VT-x/AMD-V）
- ✅ **性能更好** - 结合 PV 和 HVM 优势
- ✅ **Linux 内核支持** - 现代内核已支持
- ✅ **推荐用于新部署** - 未来方向

### 4.3 PVH 配置示例

```bash
# /etc/xen/my-pvh-vm.cfg

# 指定 PVH 模式
type = "pvh"

name = "my-pvh-vm"
kernel = "/boot/vmlinuz-6.0.0"
ramdisk = "/boot/initrd.img-6.0.0"
extra = "root=/dev/xvda1 ro"

memory = 2048
vcpus = 4

disk = [ '/dev/vg/myvm-disk,raw,xvda,rw' ]
vif = [ 'bridge=xenbr0' ]
```

## 五、HVM 模式配置（完全虚拟化）

### 5.1 基本配置

**位置**: `xen/tools/examples/xlexample.hvm`

```1:47:xen/tools/examples/xlexample.hvm
# =====================================================================
# Example HVM guest configuration
# =====================================================================
#
# This is a fairly minimal example of what is required for an
# HVM guest. For a more complete guide see xl.cfg(5)

# This configures an HVM rather than PV guest
type = "hvm"

# Guest name
name = "example.hvm"

# 128-bit UUID for the domain as a hexadecimal number.
# Use "uuidgen" to generate one if required.
# The default behavior is to generate a new UUID each time the guest is started.
#uuid = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX"

# Enable Microsoft Hyper-V compatibile paravirtualisation /
# enlightenment interfaces. Turning this on can improve Windows guest
# performance and is therefore recommended
#viridian = 1

# Initial memory allocation (MB)
memory = 128

# Maximum memory (MB)
# If this is greater than `memory' then the slack will start ballooned
# (this assumes guest kernel support for ballooning)
#maxmem = 512

# Number of VCPUS
vcpus = 2

# Network devices
# A list of 'vifspec' entries as described in
# docs/misc/xl-network-configuration.markdown
vif = [ '' ]

# Disk Devices
# A list of `diskspec' entries as described in
# docs/misc/xl-disk-configuration.txt
disk = [ '/dev/vg/guest-volume,raw,xvda,rw' ]

# Guest VGA console configuration, either SDL or VNC
sdl = 1
#vnc = 1
```

### 5.2 HVM 模式特点

- ✅ **可以运行未修改的操作系统** - Windows、未修改的 Linux 等
- ✅ **硬件兼容性好** - 模拟完整硬件环境
- ⚠️ **需要硬件虚拟化支持** - Intel VT-x 或 AMD-V
- ⚠️ **性能开销较大** - 需要模拟硬件

### 5.3 HVM 配置示例（Windows）

```bash
# /etc/xen/windows-vm.cfg

type = "hvm"
name = "windows-vm"

# Windows 优化
viridian = 1  # 启用 Hyper-V 兼容接口

memory = 4096
maxmem = 8192
vcpus = 4

# 磁盘（使用文件或块设备）
disk = [
    'file:/var/lib/xen/images/windows.img,qcow2,hda,rw',
    'file:/var/lib/xen/images/windows-cd.iso,raw,hdc:cdrom,r'
]

# 网络
vif = [ 'bridge=xenbr0,model=e1000' ]

# VNC 控制台
vnc = 1
vnclisten = "0.0.0.0"
vncdisplay = 1
vncpasswd = ""

# 启动顺序
boot = "c"  # 从硬盘启动
```

## 六、磁盘配置

### 6.1 磁盘配置格式

磁盘配置使用 `diskspec` 格式：

```
disk = [ 'BACKEND,FORMAT,VDEV,MODE' ]
```

**参数说明**:
- **BACKEND**: 后端设备或文件
  - `/dev/vg/volume` - 块设备
  - `file:/path/to/file` - 文件
  - `phy:/dev/sdX` - 物理设备
- **FORMAT**: 磁盘格式
  - `raw` - 原始格式
  - `qcow2` - QEMU 格式
  - `vhd` - VHD 格式
- **VDEV**: 虚拟设备名
  - `xvda`, `xvdb` - PV 设备
  - `hda`, `hdb` - HVM 设备
- **MODE**: 访问模式
  - `r` - 只读
  - `w` - 读写

### 6.2 磁盘配置示例

```bash
# 使用 LVM 卷
disk = [ '/dev/vg/myvm-disk,raw,xvda,rw' ]

# 使用文件
disk = [ 'file:/var/lib/xen/images/myvm.img,qcow2,xvda,rw' ]

# 多个磁盘
disk = [
    '/dev/vg/myvm-root,raw,xvda,rw',
    '/dev/vg/myvm-data,raw,xvdb,rw',
    'file:/var/lib/xen/images/myvm-swap.img,raw,xvdc,rw'
]

# CD-ROM（只读）
disk = [ 'file:/path/to/iso.iso,raw,xvdd:cdrom,r' ]
```

## 七、网络配置

### 7.1 网络配置格式

网络配置使用 `vifspec` 格式：

```
vif = [ 'OPTIONS' ]
```

### 7.2 网络配置选项

**基本选项**:
- `bridge=BRIDGE_NAME` - 桥接网络
- `mac=MAC_ADDRESS` - MAC 地址
- `model=MODEL` - 网卡模型（HVM）

**高级选项**:
- `backend=DOMAIN_ID` - 后端 Domain（默认 Domain 0）
- `script=SCRIPT` - 网络脚本
- `ip=IP_ADDRESS` - IP 地址（某些脚本）

### 7.3 网络配置示例

```bash
# 基本桥接网络
vif = [ 'bridge=xenbr0' ]

# 指定 MAC 地址
vif = [ 'bridge=xenbr0,mac=00:16:3e:12:34:56' ]

# 多个网络接口
vif = [
    'bridge=xenbr0',
    'bridge=xenbr1,mac=00:16:3e:12:34:57'
]

# HVM 使用特定网卡模型
vif = [ 'bridge=xenbr0,model=e1000' ]

# 使用自定义脚本
vif = [ 'bridge=xenbr0,script=/etc/xen/scripts/custom-vif' ]
```

### 7.4 创建网络桥接

在 Domain 0 中创建桥接：

```bash
# 安装桥接工具
sudo apt-get install bridge-utils  # Debian/Ubuntu

# 创建桥接
sudo brctl addbr xenbr0
sudo brctl addif xenbr0 eth0
sudo ifconfig xenbr0 up

# 持久化配置（Debian/Ubuntu）
# 编辑 /etc/network/interfaces
auto xenbr0
iface xenbr0 inet dhcp
    bridge_ports eth0
    bridge_stp off
```

## 八、创建和管理 Domain U

### 8.1 创建 Domain U

```bash
# 从配置文件创建
xl create /etc/xen/myvm.cfg

# 创建后连接到控制台
xl create -c /etc/xen/myvm.cfg

# 创建后自动启动 VNC 查看器
xl create -V /etc/xen/myvm.cfg

# 创建后保持暂停状态
xl create -p /etc/xen/myvm.cfg
```

### 8.2 查看 Domain 列表

```bash
# 列出所有 Domain
xl list

# 详细列表
xl list -l

# 显示特定 Domain
xl list myvm

# 显示 CPU 池信息
xl list -c

# 显示 NUMA 信息
xl list -n
```

**输出示例**:
```
Name                                        ID   Mem VCPUs      State   Time(s)
Domain-0                                     0  1024     2     r-----     123.4
myvm                                         1   512     2     -b----      45.6
```

### 8.3 启动和停止

```bash
# 启动 Domain（从配置文件）
xl create /etc/xen/myvm.cfg

# 停止 Domain（正常关闭）
xl shutdown myvm

# 强制停止 Domain
xl destroy myvm

# 停止所有 Domain
xl shutdown -a
```

### 8.4 暂停和恢复

```bash
# 暂停 Domain
xl pause myvm

# 恢复 Domain
xl unpause myvm

# 检查状态
xl list myvm
```

### 8.5 重启

```bash
# 重启 Domain
xl reboot myvm

# 等待重启完成
xl reboot -w myvm

# 重启所有 Domain
xl reboot -a
```

## 九、资源管理

### 9.1 CPU 管理

#### 查看 CPU 信息

```bash
# 查看物理 CPU 信息
xl info

# 查看 Domain CPU 使用情况
xl top

# 查看特定 Domain 的 VCPU 信息
xl vcpu-list myvm
```

#### 动态调整 VCPU

```bash
# 设置 VCPU 数量（需要 Domain 支持热插拔）
xl set-vcpus myvm 4

# 设置 CPU 亲和性
xl vcpu-pin myvm 0 0-1  # VCPU 0 绑定到 CPU 0-1
xl vcpu-pin myvm all 2-3  # 所有 VCPU 绑定到 CPU 2-3
```

#### CPU 权重和上限

在配置文件中：

```bash
# CPU 权重（相对优先级）
cpus = "0-3"
cpu_weight = 256  # 默认 256

# CPU 上限（百分比）
cpu_cap = 50  # 限制为 50% CPU
```

### 9.2 内存管理

#### 查看内存信息

```bash
# 查看物理内存
xl info | grep total_memory

# 查看 Domain 内存使用
xl list -l myvm

# 查看内存统计
xl mem-set myvm 2048  # 设置内存（MB）
```

#### 内存气球（Ballooning）

如果配置了 `maxmem > memory`，支持动态调整：

```bash
# 增加内存
xl mem-set myvm 2048

# 减少内存
xl mem-set myvm 1024
```

**要求**: Guest 内核必须支持内存气球驱动。

### 9.3 存储管理

#### 添加磁盘

```bash
# 在线添加磁盘（需要 Guest 支持）
xl block-attach myvm \
    phy:/dev/vg/new-disk \
    xvdb \
    w
```

#### 移除磁盘

```bash
xl block-detach myvm xvdb
```

#### 查看磁盘信息

```bash
xl block-list myvm
```

### 9.4 网络管理

#### 添加网络接口

```bash
xl network-attach myvm bridge=xenbr1
```

#### 移除网络接口

```bash
xl network-detach myvm vif1.0
```

#### 查看网络信息

```bash
xl network-list myvm
```

## 十、控制台访问

### 10.1 控制台类型

**PV/PVH Domain**:
- 使用 `hvc0` 控制台
- 通过 `xl console` 访问

**HVM Domain**:
- 使用 VNC 或 SDL
- 需要图形界面支持

### 10.2 访问控制台

```bash
# 连接到控制台
xl console myvm

# 连接到控制台（非阻塞）
xl console -t myvm

# 退出控制台
# 按 Ctrl + ]
```

### 10.3 VNC 配置（HVM）

在配置文件中：

```bash
# 启用 VNC
vnc = 1
vnclisten = "0.0.0.0"  # 监听所有接口
vncdisplay = 1  # VNC 显示号
vncpasswd = "password"  # VNC 密码（可选）

# 或使用 SDL
sdl = 1
```

访问 VNC：

```bash
# 查看 VNC 信息
xl vncviewer myvm

# 或使用 VNC 客户端
vncviewer host:5901  # 5900 + vncdisplay
```

## 十一、监控和调试

### 11.1 监控工具

#### xl top

```bash
# 类似 top 的监控工具
xl top

# 显示所有 Domain
xl top -a
```

#### xentop

```bash
# 更详细的监控工具
xentop
```

#### xl dmesg

```bash
# 查看 Domain 的内核消息
xl dmesg myvm
```

### 11.2 日志和跟踪

```bash
# 查看 Xen 日志
xl debug-keys  # 触发调试信息

# 跟踪工具
xentrace  # 启用跟踪
xentrace_format  # 格式化跟踪数据
```

### 11.3 性能分析

```bash
# 查看 Domain 信息
xl info

# 查看特定 Domain 详细信息
xl dominfo myvm

# 查看 VCPU 信息
xl vcpu-list myvm
```

## 十二、高级配置

### 12.1 启动行为

```bash
# 关机行为
on_poweroff = "destroy"    # 销毁
on_poweroff = "restart"    # 重启
on_poweroff = "preserve"   # 保留

# 重启行为
on_reboot = "restart"      # 重启
on_reboot = "destroy"     # 销毁

# 崩溃行为
on_crash = "destroy"      # 销毁
on_crash = "restart"      # 重启
on_crash = "preserve"     # 保留（用于调试）
```

### 12.2 自动启动

```bash
# 在配置文件中启用自动启动
on_poweroff = "destroy"
on_reboot = "restart"

# 使用 xendomains 服务
# 编辑 /etc/xen/auto/ 目录
# 创建符号链接到配置文件
ln -s /etc/xen/myvm.cfg /etc/xen/auto/myvm.cfg

# 启用 xendomains 服务
systemctl enable xendomains.service
```

### 12.3 CPU 池（Cpupool）

```bash
# 创建 CPU 池
xl cpupool-create name=pool1 cpus="4-7"

# 将 Domain 移到 CPU 池
xl cpupool-migrate myvm pool1

# 查看 CPU 池
xl cpupool-list
```

### 12.4 NUMA 配置

```bash
# 绑定到特定 NUMA 节点
cpus = "0-3"  # 使用 CPU 0-3（假设在 NUMA 节点 0）
numa = [ "0-3" ]  # 明确指定 NUMA 节点
```

## 十三、常见维护任务

### 13.1 备份 Domain U

```bash
# 暂停 Domain
xl pause myvm

# 备份磁盘
dd if=/dev/vg/myvm-disk of=/backup/myvm-backup.img bs=1M

# 恢复 Domain
xl unpause myvm
```

### 13.2 迁移 Domain U

```bash
# 检查迁移能力
xl migrate-check myvm target-host

# 执行迁移
xl migrate myvm target-host

# 迁移并保留原 Domain
xl migrate -l myvm target-host
```

### 13.3 快照管理

```bash
# 创建快照（使用支持快照的存储后端）
# 例如使用 LVM
lvcreate -L 10G -s -n myvm-snapshot /dev/vg/myvm-disk

# 恢复快照
lvconvert --merge /dev/vg/myvm-snapshot
```

### 13.4 更新配置

```bash
# 更新运行中 Domain 的配置
xl config-update myvm /etc/xen/myvm-new.cfg

# 注意：某些配置需要重启才能生效
```

## 十四、故障排除

### 14.1 Domain 无法启动

**检查清单**:
1. 检查配置文件语法: `xl create -n /etc/xen/myvm.cfg`（干运行）
2. 检查资源是否足够: `xl info`
3. 检查日志: `xl dmesg`
4. 检查磁盘和网络配置

### 14.2 网络无法工作

```bash
# 检查网络后端
xl network-list myvm

# 检查桥接
brctl show

# 检查 xenstore
xenstore-ls /local/domain/1/device/vif
```

### 14.3 性能问题

```bash
# 检查 CPU 使用
xl top

# 检查内存使用
xl list -l

# 检查 I/O
xl dmesg | grep -i disk
```

## 十五、最佳实践

### 15.1 配置建议

1. **使用有意义的名称**: `name = "web-server-01"`
2. **合理分配资源**: 不要过度分配
3. **启用自动重启**: `on_reboot = "restart"`
4. **使用 UUID**: 便于管理
5. **文档化配置**: 添加注释说明

### 15.2 安全建议

1. **限制资源**: 使用 `cpu_cap` 和内存限制
2. **网络隔离**: 使用不同的桥接网络
3. **定期更新**: 更新 Guest 操作系统
4. **备份重要数据**: 定期备份

### 15.3 性能优化

1. **选择合适的虚拟化模式**: Linux 使用 PVH，Windows 使用 HVM
2. **CPU 亲和性**: 绑定到特定 CPU
3. **NUMA 感知**: 绑定到同一 NUMA 节点
4. **存储优化**: 使用高性能存储后端

## 十六、参考

- `xen/tools/examples/` - 配置示例
- `xen/docs/man/xl.cfg.5.pod.in` - 配置文件手册
- [Xen Project Wiki - Guest Configuration](https://wiki.xenproject.org/wiki/Guest_Configuration)
- [Domain 0 的本质](./domain0-essence.md) - Domain 0 概念
- [Xen 虚拟化模式详解](./xen-virtualization-modes.md) - 虚拟化模式说明
