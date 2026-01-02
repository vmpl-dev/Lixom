# Xen Hypercall 笔记

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/include/public/xen.h` - Hypercall 编号定义
- `xen/xen/include/hypercall-defs.c` - Hypercall 定义和路由表
- `xen/xen/include/xen/hypercall.h` - Hypercall 头文件
- `xen/xen/arch/*/hypercall.c` - 架构特定的 hypercall 实现

## 概述

**Hypercall** 是 Guest 操作系统调用 Xen Hypervisor 服务的接口，类似于系统调用（syscall）。Guest 通过 hypercall 请求 Hypervisor 执行特权操作，如内存管理、调度、事件通道等。

## 一、Hypercall 基础

### 1.1 什么是 Hypercall？

- **Hypercall** = Hypervisor + System Call
- Guest 操作系统通过 hypercall 请求 Hypervisor 服务
- 类似于系统调用，但跨越虚拟化边界
- 最多支持 5 个参数

### 1.2 Hypercall 调用方式

**x86 架构**:
- **32-bit PV**: `INT 0x82`
- **64-bit PV**: `SYSCALL`
- **Intel HVM**: `VMCALL`
- **AMD HVM**: `VMMCALL`

**ARM 架构**:
- 使用 `HVC` 指令
- Hypercall 编号在 `r12` (arm32) 或 `x16` (arm64)

**Hypercall Page**:
- Xen 在 Guest 内存中写入 hypercall stub
- Guest 通过 `call hypercall_page + index * 32` 调用
- 抽象了不同模式和厂商的差异

### 1.3 Hypercall 编号定义

所有 hypercall 编号定义在 `xen/xen/include/public/xen.h`:

```77:118:xen/xen/include/public/xen.h
#define __HYPERVISOR_set_trap_table        0
#define __HYPERVISOR_mmu_update            1
#define __HYPERVISOR_set_gdt               2
#define __HYPERVISOR_stack_switch          3
#define __HYPERVISOR_set_callbacks         4
#define __HYPERVISOR_fpu_taskswitch        5
#define __HYPERVISOR_sched_op_compat       6 /* compat since 0x00030101 */
#define __HYPERVISOR_platform_op           7
#define __HYPERVISOR_set_debugreg          8
#define __HYPERVISOR_get_debugreg          9
#define __HYPERVISOR_update_descriptor    10
#define __HYPERVISOR_memory_op            12
#define __HYPERVISOR_multicall            13
#define __HYPERVISOR_update_va_mapping    14
#define __HYPERVISOR_set_timer_op         15
#define __HYPERVISOR_event_channel_op_compat 16 /* compat since 0x00030202 */
#define __HYPERVISOR_xen_version          17
#define __HYPERVISOR_console_io           18
#define __HYPERVISOR_physdev_op_compat    19 /* compat since 0x00030202 */
#define __HYPERVISOR_grant_table_op       20
#define __HYPERVISOR_vm_assist            21
#define __HYPERVISOR_update_va_mapping_otherdomain 22
#define __HYPERVISOR_iret                 23 /* x86 only */
#define __HYPERVISOR_vcpu_op              24
#define __HYPERVISOR_set_segment_base     25 /* x86/64 only */
#define __HYPERVISOR_mmuext_op            26
#define __HYPERVISOR_xsm_op               27
#define __HYPERVISOR_nmi_op               28
#define __HYPERVISOR_sched_op             29
#define __HYPERVISOR_callback_op          30
#define __HYPERVISOR_xenoprof_op          31
#define __HYPERVISOR_event_channel_op     32
#define __HYPERVISOR_physdev_op           33
#define __HYPERVISOR_hvm_op               34
#define __HYPERVISOR_sysctl               35
#define __HYPERVISOR_domctl               36
#define __HYPERVISOR_kexec_op             37
#define __HYPERVISOR_tmem_op              38
#define __HYPERVISOR_argo_op              39
#define __HYPERVISOR_xenpmu_op            40
#define __HYPERVISOR_dm_op                41
#define __HYPERVISOR_hypfs_op             42
```

**架构特定的 hypercall**:
```120:128:xen/xen/include/public/xen.h
/* Architecture-specific hypercall definitions. */
#define __HYPERVISOR_arch_0               48
#define __HYPERVISOR_arch_1               49
#define __HYPERVISOR_arch_2               50
#define __HYPERVISOR_arch_3               51
#define __HYPERVISOR_arch_4               52
#define __HYPERVISOR_arch_5               53
#define __HYPERVISOR_arch_6               54
#define __HYPERVISOR_arch_7               55
```

## 二、Hypercall 列表和实现位置

### 2.1 核心 Hypercall

#### 0. `HYPERVISOR_set_trap_table` - 设置陷阱表

**功能**: 设置 Guest 的异常/中断处理表

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_set_trap_table()`
- `xen/xen/arch/x86/pv/misc-hypercalls.c` - 兼容实现

**说明**: 仅 PV Guest 需要，HVM Guest 使用硬件中断

---

#### 1. `HYPERVISOR_mmu_update` - MMU 更新

**功能**: 批量更新页表项

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_mmu_update()`

**说明**: PV Guest 用于更新页表，HVM Guest 不需要

---

#### 2. `HYPERVISOR_set_gdt` - 设置 GDT

**功能**: 设置全局描述符表（GDT）

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_set_gdt()`

**说明**: x86 架构特定，ARM 不需要

---

#### 3. `HYPERVISOR_stack_switch` - 切换栈

**功能**: 切换内核栈

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_stack_switch()`

---

#### 4. `HYPERVISOR_set_callbacks` - 设置回调

**功能**: 设置事件回调函数地址

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_set_callbacks()`

---

#### 5. `HYPERVISOR_fpu_taskswitch` - FPU 任务切换

**功能**: FPU 上下文切换

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_fpu_taskswitch()`

---

#### 6. `HYPERVISOR_sched_op_compat` - 调度操作（兼容）

**功能**: 旧版调度操作（已弃用）

**支持架构**: x86 PV, ARM

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_sched_op_compat()`
- `xen/xen/common/sched/core.c` - 通用实现

---

#### 7. `HYPERVISOR_platform_op` - 平台操作

**功能**: 平台特定操作（ACPI、时间等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/arch/x86/platform_hypercall.c` - `do_platform_op()`
- `xen/xen/arch/arm/platform_hypercall.c` - ARM 实现

---

#### 8-9. `HYPERVISOR_set_debugreg` / `HYPERVISOR_get_debugreg` - 调试寄存器

**功能**: 设置/获取调试寄存器

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_set_debugreg()`, `do_get_debugreg()`

---

#### 10. `HYPERVISOR_update_descriptor` - 更新描述符

**功能**: 更新段描述符

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_update_descriptor()`

---

#### 12. `HYPERVISOR_memory_op` - 内存操作

**功能**: 内存管理操作（分配、映射等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/memory.c` - `do_memory_op()`
- `xen/xen/arch/x86/hvm/hypercall.c` - HVM 实现

**子操作**:
- `XENMEM_increase_reservation` - 增加内存预留
- `XENMEM_decrease_reservation` - 减少内存预留
- `XENMEM_populate_physmap` - 填充物理映射
- `XENMEM_add_to_physmap` - 添加到物理映射
- 等等

---

#### 13. `HYPERVISOR_multicall` - 多调用

**功能**: 批量执行多个 hypercall

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/multicall.c` - `do_multicall()`

**说明**: 减少 hypercall 开销，批量执行

---

#### 14. `HYPERVISOR_update_va_mapping` - 更新虚拟地址映射

**功能**: 更新单个虚拟地址的页表项

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_update_va_mapping()`

---

#### 15. `HYPERVISOR_set_timer_op` - 设置定时器

**功能**: 设置虚拟定时器

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/sched/core.c` - `do_set_timer_op()`

---

#### 16. `HYPERVISOR_event_channel_op_compat` - 事件通道操作（兼容）

**功能**: 旧版事件通道操作（已弃用）

**支持架构**: x86 PV, ARM

**实现位置**:
- `xen/xen/common/event_channel.c` - 兼容实现

---

#### 17. `HYPERVISOR_xen_version` - Xen 版本

**功能**: 获取 Xen 版本信息

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/kernel.c` - `do_xen_version()`

---

#### 18. `HYPERVISOR_console_io` - 控制台 I/O

**功能**: 控制台读写操作

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/kernel.c` - `do_console_io()`

---

#### 19. `HYPERVISOR_physdev_op_compat` - 物理设备操作（兼容）

**功能**: 旧版物理设备操作（已弃用）

**支持架构**: x86 PV, ARM

**实现位置**:
- `xen/xen/common/irq.c` - 兼容实现

---

#### 20. `HYPERVISOR_grant_table_op` - Grant 表操作

**功能**: Grant 表操作（共享内存机制）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/grant_table.c` - `do_grant_table_op()`

**说明**: 用于域间共享内存

---

#### 21. `HYPERVISOR_vm_assist` - 虚拟机辅助

**功能**: 启用/禁用虚拟机辅助功能

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/domain.c` - `do_vm_assist()`

---

#### 22. `HYPERVISOR_update_va_mapping_otherdomain` - 更新其他域的虚拟地址映射

**功能**: 更新其他域的页表项

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_update_va_mapping_otherdomain()`

---

#### 23. `HYPERVISOR_iret` - 中断返回

**功能**: 从中断/异常返回

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_iret()`

---

#### 24. `HYPERVISOR_vcpu_op` - VCPU 操作

**功能**: VCPU 相关操作

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/domain.c` - `common_vcpu_op()`
- `xen/xen/include/xen/hypercall.h` - 声明

---

#### 25. `HYPERVISOR_set_segment_base` - 设置段基址

**功能**: 设置段基址寄存器

**支持架构**: x86/64 PV

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_set_segment_base()`

---

#### 26. `HYPERVISOR_mmuext_op` - MMU 扩展操作

**功能**: MMU 扩展操作（TLB 刷新、页表固定等）

**支持架构**: x86 PV (32/64), HVM

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_mmuext_op()`

---

#### 27. `HYPERVISOR_xsm_op` - XSM 操作

**功能**: Xen 安全模块（XSM/FLASK）操作

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/xsm/xsm_core.c` - `do_xsm_op()`
- `xen/xen/xsm/flask/flask_op.c` - FLASK 实现

---

#### 28. `HYPERVISOR_nmi_op` - NMI 操作

**功能**: 不可屏蔽中断操作

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_nmi_op()`

---

#### 29. `HYPERVISOR_sched_op` - 调度操作

**功能**: 调度器操作（阻塞、唤醒等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/sched/core.c` - `do_sched_op()`

---

#### 30. `HYPERVISOR_callback_op` - 回调操作

**功能**: 回调函数操作

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_callback_op()`

---

#### 31. `HYPERVISOR_xenoprof_op` - XenOprofile 操作

**功能**: 性能分析操作

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/common/xenoprof.c` - `do_xenoprof_op()`

---

#### 32. `HYPERVISOR_event_channel_op` - 事件通道操作

**功能**: 事件通道操作（创建、绑定、发送等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/event_channel.c` - `do_event_channel_op()`

---

#### 33. `HYPERVISOR_physdev_op` - 物理设备操作

**功能**: 物理设备操作（中断、PCI 等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/irq.c` - `pci_physdev_op()`
- `xen/xen/include/xen/hypercall.h` - 声明

---

#### 34. `HYPERVISOR_hvm_op` - HVM 操作

**功能**: HVM Guest 特定操作

**支持架构**: x86 HVM

**实现位置**:
- `xen/xen/arch/x86/hvm/hypercall.c` - `do_hvm_op()`

---

#### 35. `HYPERVISOR_sysctl` - 系统控制

**功能**: 系统级控制操作（需要特权）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/sysctl.c` - `do_sysctl()`

**说明**: 通常只有 Domain 0 可以使用

---

#### 36. `HYPERVISOR_domctl` - Domain 控制

**功能**: Domain 级控制操作（需要特权）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/domctl.c` - `do_domctl()`

**说明**: 通常只有 Domain 0 可以使用，用于创建、管理 Domain

---

#### 37. `HYPERVISOR_kexec_op` - Kexec 操作

**功能**: Kexec 相关操作

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/common/kexec.c` - `do_kexec_op()`

---

#### 38. `HYPERVISOR_tmem_op` - Transcedent Memory 操作

**功能**: Transcedent Memory 操作（已弃用）

**支持架构**: 无（已移除）

**说明**: 此功能已被移除

---

#### 39. `HYPERVISOR_argo_op` - Argo 操作

**功能**: Argo 域间通信操作

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/argo.c` - `do_argo_op()`

---

#### 40. `HYPERVISOR_xenpmu_op` - Xen PMU 操作

**功能**: 性能监控单元（PMU）操作

**支持架构**: x86 (PV/HVM)

**实现位置**:
- `xen/xen/arch/x86/hvm/hypercall.c` - PMU 相关

---

#### 41. `HYPERVISOR_dm_op` - Device Model 操作

**功能**: 设备模型操作（IOREQ 服务器等）

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/dm.c` - `do_dm_op()`

---

#### 42. `HYPERVISOR_hypfs_op` - Hypervisor 文件系统操作

**功能**: Hypervisor 文件系统操作

**支持架构**: x86 (PV/HVM), ARM

**实现位置**:
- `xen/xen/common/hypfs.c` - `do_hypfs_op()`

---

### 2.2 架构特定的 Hypercall

#### `HYPERVISOR_arch_0` - `HYPERVISOR_arch_7`

**功能**: 架构特定的 hypercall 槽位

**实现位置**:
- `xen/xen/arch/x86/hypercall.c` - x86 实现
- `xen/xen/arch/arm/traps.c` - ARM 实现

**x86 特定**:
- `HYPERVISOR_arch_1` = `paging_domctl_continuation` (分页 Domain 控制延续)

**ARM 特定**:
- `HYPERVISOR_arch_0`, `HYPERVISOR_arch_1` - 已定义

---

### 2.3 x86 特定的 Hypercall

#### `HYPERVISOR_mca` - Machine Check Architecture

**功能**: 机器检查架构操作

**支持架构**: x86 PV (32/64)

**实现位置**:
- `xen/xen/arch/x86/pv/hypercall.c` - `do_mca()`

---

## 三、Hypercall 路由表

Hypercall 路由表定义在 `xen/xen/include/hypercall-defs.c`，指定了不同架构和模式下的实现：

```229:298:xen/xen/include/hypercall-defs.c
table:                             pv32     pv64     hvm32    hvm64    arm
set_trap_table                     compat   do       -        -        -
mmu_update                         do:1     do:1     -        -        -
set_gdt                            compat   do       -        -        -
stack_switch                       do:2     do:2     -        -        -
set_callbacks                      compat   do       -        -        -
fpu_taskswitch                     do       do       -        -        -
sched_op_compat                    do       do       -        -        dep
#ifndef CONFIG_PV_SHIM_EXCLUSIVE
platform_op                        compat   do       compat   do       do
#endif
set_debugreg                       do       do       -        -        -
get_debugreg                       do       do       -        -        -
update_descriptor                  compat   do       -        -        -
memory_op                          compat   do       hvm      hvm      do
multicall                          compat:2 do:2     compat   do       do
update_va_mapping                  compat   do       -        -        -
set_timer_op                       compat   do       compat   do       -
event_channel_op_compat            do       do       -        -        dep
xen_version                        do       do       do       do       do
console_io                         do       do       do       do       do
physdev_op_compat                  compat   do       -        -        dep
#if defined(CONFIG_GRANT_TABLE)
grant_table_op                     compat   do       hvm      hvm      do
#elif defined(CONFIG_PV_SHIM)
grant_table_op                     compat   do       -        -        -
#endif
vm_assist                          do       do       do       do       do
update_va_mapping_otherdomain      compat   do       -        -        -
iret                               compat:1 do:1     -        -        -
vcpu_op                            compat   do       compat:1 do:1     do
set_segment_base                   do:2     do:2     -        -        -
#ifdef CONFIG_PV
mmuext_op                          compat:2 do:2     compat   do       -
#endif
xsm_op                             compat   do       compat   do       do
nmi_op                             compat   do       -        -        -
sched_op                           compat   do       compat   do       do
callback_op                        compat   do       -        -        -
#ifdef CONFIG_XENOPROF
xenoprof_op                        compat   do       -        -        -
#endif
event_channel_op                   do       do       do:1     do:1     do:1
physdev_op                         compat   do       hvm      hvm      do_arm
#ifdef CONFIG_HVM
hvm_op                             do       do       do       do       do
#endif
#ifndef CONFIG_PV_SHIM_EXCLUSIVE
sysctl                             do       do       do       do       do
domctl                             do       do       do       do       do
#endif
#ifdef CONFIG_KEXEC
kexec_op                           compat   do       -        -        -
#endif
tmem_op                            -        -        -        -        -
#ifdef CONFIG_ARGO
argo_op                            compat   do       compat   do       do
#endif
xenpmu_op                          do       do       do       do       -
#ifdef CONFIG_IOREQ_SERVER
dm_op                              compat   do       compat   do       do
#endif
#ifdef CONFIG_HYPFS
hypfs_op                           do       do       do       do       do
#endif
mca                                do       do       -        -        -
#ifndef CONFIG_PV_SHIM_EXCLUSIVE
paging_domctl_cont                 do       do       do       do       -
#endif
```

**说明**:
- `do` - 标准实现
- `compat` - 兼容实现（32位）
- `hvm` - HVM 特定实现
- `do_arm` - ARM 特定实现
- `dep` - 已弃用
- `-` - 不支持
- 数字表示优先级（如 `do:1` 表示优先级 1）

## 四、关键文件位置

### 4.1 定义文件

- **`xen/xen/include/public/xen.h`**: Hypercall 编号定义
- **`xen/xen/include/hypercall-defs.c`**: Hypercall 定义和路由表
- **`xen/xen/include/xen/hypercall.h`**: Hypercall 头文件

### 4.2 通用实现

- **`xen/xen/common/memory.c`**: `do_memory_op()`
- **`xen/xen/common/domctl.c`**: `do_domctl()`
- **`xen/xen/common/sysctl.c`**: `do_sysctl()`
- **`xen/xen/common/event_channel.c`**: `do_event_channel_op()`
- **`xen/xen/common/grant_table.c`**: `do_grant_table_op()`
- **`xen/xen/common/sched/core.c`**: `do_sched_op()`, `do_set_timer_op()`
- **`xen/xen/common/kernel.c`**: `do_xen_version()`, `do_console_io()`
- **`xen/xen/common/multicall.c`**: `do_multicall()`
- **`xen/xen/common/argo.c`**: `do_argo_op()`
- **`xen/xen/common/dm.c`**: `do_dm_op()`
- **`xen/xen/common/hypfs.c`**: `do_hypfs_op()`
- **`xen/xen/common/domain.c`**: `do_vm_assist()`, `common_vcpu_op()`
- **`xen/xen/common/kexec.c`**: `do_kexec_op()`
- **`xen/xen/common/irq.c`**: `pci_physdev_op()`
- **`xen/xen/common/xenoprof.c`**: `do_xenoprof_op()`

### 4.3 x86 特定实现

- **`xen/xen/arch/x86/pv/hypercall.c`**: PV hypercall 实现
- **`xen/xen/arch/x86/pv/misc-hypercalls.c`**: PV 其他 hypercall
- **`xen/xen/arch/x86/hvm/hypercall.c`**: HVM hypercall 实现
- **`xen/xen/arch/x86/platform_hypercall.c`**: 平台 hypercall
- **`xen/xen/arch/x86/hypercall.c`**: x86 通用 hypercall 路由

### 4.4 ARM 特定实现

- **`xen/xen/arch/arm/traps.c`**: ARM hypercall 入口
- **`xen/xen/arch/arm/platform_hypercall.c`**: ARM 平台 hypercall

### 4.5 XSM/FLASK 实现

- **`xen/xen/xsm/xsm_core.c`**: `do_xsm_op()`
- **`xen/xen/xsm/flask/flask_op.c`**: FLASK 实现

## 五、Hypercall 分类

### 5.1 按功能分类

**内存管理**:
- `HYPERVISOR_memory_op`
- `HYPERVISOR_mmu_update`
- `HYPERVISOR_update_va_mapping`
- `HYPERVISOR_mmuext_op`

**调度**:
- `HYPERVISOR_sched_op`
- `HYPERVISOR_set_timer_op`

**事件和中断**:
- `HYPERVISOR_event_channel_op`
- `HYPERVISOR_set_callbacks`
- `HYPERVISOR_physdev_op`

**域管理**（特权）:
- `HYPERVISOR_domctl`
- `HYPERVISOR_sysctl`
- `HYPERVISOR_vcpu_op`

**通信**:
- `HYPERVISOR_grant_table_op`
- `HYPERVISOR_argo_op`
- `HYPERVISOR_console_io`

**x86 特定**:
- `HYPERVISOR_set_trap_table`
- `HYPERVISOR_set_gdt`
- `HYPERVISOR_iret`
- `HYPERVISOR_mca`

### 5.2 按架构支持分类

**所有架构支持**:
- `HYPERVISOR_memory_op`
- `HYPERVISOR_multicall`
- `HYPERVISOR_xen_version`
- `HYPERVISOR_console_io`
- `HYPERVISOR_grant_table_op`
- `HYPERVISOR_vm_assist`
- `HYPERVISOR_vcpu_op`
- `HYPERVISOR_sched_op`
- `HYPERVISOR_event_channel_op`
- `HYPERVISOR_sysctl`
- `HYPERVISOR_domctl`
- `HYPERVISOR_argo_op`
- `HYPERVISOR_dm_op`
- `HYPERVISOR_hypfs_op`

**仅 x86 PV**:
- `HYPERVISOR_set_trap_table`
- `HYPERVISOR_mmu_update`
- `HYPERVISOR_set_gdt`
- `HYPERVISOR_update_va_mapping`
- `HYPERVISOR_iret`
- `HYPERVISOR_mca`

**仅 x86 HVM**:
- `HYPERVISOR_hvm_op`

**仅 ARM**:
- ARM 使用硬件虚拟化，不需要很多 PV 特定的 hypercall

## 六、Hypercall 调用流程

### 6.1 Guest 调用流程

1. Guest 准备参数
2. 调用 hypercall（通过 INT/SYSCALL/VMCALL/HVC）
3. 进入 Hypervisor
4. Hypervisor 路由到对应的处理函数
5. 执行操作
6. 返回结果给 Guest

### 6.2 Hypervisor 处理流程

1. **入口**: 架构特定的 hypercall 入口（如 `xen/xen/arch/x86/hypercall.c`）
2. **路由**: 根据 hypercall 编号路由到处理函数
3. **权限检查**: XSM/FLASK 权限检查
4. **执行**: 调用对应的 `do_*` 函数
5. **返回**: 返回结果给 Guest

## 七、参考

- `xen/xen/include/public/xen.h` - Hypercall 编号定义
- `xen/xen/include/hypercall-defs.c` - Hypercall 定义和路由
- `xen/docs/guest-guide/x86/hypercall-abi.rst` - Hypercall ABI 文档
- `xen/xen/include/public/arch-arm.h` - ARM Hypercall 说明
- [Xen Project Wiki - Hypercalls](https://wiki.xenproject.org/wiki/Hypercall)
