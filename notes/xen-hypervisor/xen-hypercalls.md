# Xen Hypercall 笔记

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/include/public/xen.h` - Hypercall 编号定义
- `xen/xen/include/hypercall-defs.c` - Hypercall 定义和路由表
- `xen/xen/include/xen/hypercall.h` - Hypercall 头文件
- `xen/xen/arch/*/hypercall.c` - 架构特定的 hypercall 实现

## 概述

**Hypercall** 是 Guest 操作系统调用 Xen Hypervisor 服务的接口，类似于系统调用（syscall）。Guest 通过 hypercall 请求 Hypervisor 执行特权操作，如内存管理、调度、事件通道等。

**重要设计模式**: Xen 的 hypercall 基于 **GFN (Guest Frame Number)**，所有指向 guest 内存的指针都是基于 GFN 的。这种设计与 AMD SEV-ES 的 GHCB 协议类似，都是在物理页中存放客户请求。详见 [Hypercall 基于 GFN 的设计模式](./hypercall-gfn-design.md)。

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

## 六、Hypercall 权限分类

### 6.1 Domain 0 专用（特权）Hypercall

以下 hypercall **通常只有 Domain 0（特权域）可以使用**，用于系统管理和控制：

#### 6.1.1 系统管理类

**`HYPERVISOR_sysctl`** - 系统控制
- **用途**: 系统级控制操作
- **权限**: 需要特权域（Domain 0）
- **功能**:
  - 读取控制台
  - 获取系统信息
  - 调度器管理
  - 性能监控
  - 系统配置
- **实现**: `xen/xen/common/sysctl.c` - `do_sysctl()`
- **权限检查**: 通过 XSM (`xsm_sysctl()`) 检查权限

**`HYPERVISOR_domctl`** - Domain 控制
- **用途**: Domain 级管理操作
- **权限**: 需要特权域（Domain 0）
- **功能**:
  - 创建、销毁 Domain
  - 暂停、恢复 Domain
  - 设置 Domain 参数（内存、VCPU 等）
  - 迁移 Domain
  - 获取 Domain 信息
- **实现**: `xen/xen/common/domctl.c` - `do_domctl()`
- **说明**: 这是 Domain 0 管理其他 Domain 的主要接口

#### 6.1.2 硬件访问类

**`HYPERVISOR_physdev_op`** - 物理设备操作（部分操作）
- **用途**: 物理设备管理
- **权限**: 部分操作需要硬件域（hardware domain，通常是 Domain 0）
- **需要硬件域的操作**:
  - `PHYSDEVOP_pci_device_add` - 添加 PCI 设备
  - `PHYSDEVOP_pci_device_remove` - 移除 PCI 设备
  - `PHYSDEVOP_pci_mmcfg_reserved` - PCI MMCONFIG 保留
  - `PHYSDEVOP_dbgp_op` - 调试端口操作
- **实现**: `xen/xen/arch/x86/physdev.c` - `do_physdev_op()`
- **权限检查**:
```68:100:xen/xen/arch/x86/hvm/hypercall.c
long hvm_physdev_op(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    const struct vcpu *curr = current;
    const struct domain *currd = curr->domain;

    switch ( cmd )
    {
    case PHYSDEVOP_map_pirq:
    case PHYSDEVOP_unmap_pirq:
    case PHYSDEVOP_eoi:
    case PHYSDEVOP_irq_status_query:
    case PHYSDEVOP_get_free_pirq:
        if ( !has_pirq(currd) )
            return -ENOSYS;
        break;

    case PHYSDEVOP_pci_mmcfg_reserved:
    case PHYSDEVOP_pci_device_add:
    case PHYSDEVOP_pci_device_remove:
    case PHYSDEVOP_dbgp_op:
        if ( !is_hardware_domain(currd) )
            return -ENOSYS;
        break;

    default:
        return -ENOSYS;
    }

    if ( !curr->hcall_compat )
        return do_physdev_op(cmd, arg);
    else
        return compat_physdev_op(cmd, arg);
}
```

**`HYPERVISOR_platform_op`** - 平台操作（部分操作）
- **用途**: 平台特定操作
- **权限**: 部分操作需要特权域
- **需要特权的操作**:
  - 设置系统时间
  - 访问 MSR
  - 其他平台特定配置

### 6.2 Domain U 可以使用（非特权）Hypercall

以下 hypercall **普通 Domain（Domain U）可以使用**，用于自身运行和管理：

#### 6.2.1 内存管理类

**`HYPERVISOR_memory_op`** - 内存操作
- **用途**: Guest 内存管理
- **权限**: 大部分操作 Domain U 可以使用
- **Domain U 可用操作**:
  - `XENMEM_increase_reservation` - 增加内存预留
  - `XENMEM_decrease_reservation` - 减少内存预留
  - `XENMEM_populate_physmap` - 填充物理映射
  - `XENMEM_add_to_physmap` - 添加到物理映射
  - `XENMEM_remove_from_physmap` - 从物理映射移除
  - `XENMEM_exchange` - 交换内存页
  - `XENMEM_maximum_ram_page` - 获取最大 RAM 页号
  - `XENMEM_current_reservation` - 获取当前预留
  - `XENMEM_maximum_reservation` - 获取最大预留
  - `XENMEM_machphys_mfn_list` - 获取机器物理页列表
  - `XENMEM_reserved` - 保留内存
  - `XENMEM_memory_map` - 内存映射（仅 PV）
  - `XENMEM_set_memory_map` - 设置内存映射（仅 PV）
  - `XENMEM_add_to_physmap_batch` - 批量添加到物理映射
  - `XENMEM_pin_page_range` - 固定页范围
  - `XENMEM_unpin_page_range` - 取消固定页范围
  - `XENMEM_get_sharing_freed_pages` - 获取共享释放页
  - `XENMEM_get_sharing_shared_pages` - 获取共享页
  - `XENMEM_paging_op` - 分页操作
  - `XENMEM_access_op` - 访问操作
  - `XENMEM_claim_pages` - 声明页
  - `XENMEM_get_vnumainfo` - 获取虚拟 NUMA 信息
  - `XENMEM_set_vnumainfo` - 设置虚拟 NUMA 信息
- **受限操作**（需要特权）:
  - `XENMEM_machine_memory_map` - 机器内存映射（仅 Domain 0）
  - `XENMEM_machphys_mapping` - 机器物理映射（仅 Domain 0）

#### 6.2.2 通信类

**`HYPERVISOR_grant_table_op`** - Grant Table 操作
- **用途**: 内存共享和授权
- **权限**: Domain U 可以使用
- **功能**:
  - 设置 Grant Table
  - 授权内存页给其他 Domain
  - 映射其他 Domain 授权的内存页
  - 取消授权和映射
- **实现**: `xen/xen/common/grant_table.c` - `do_grant_table_op()`

**`HYPERVISOR_event_channel_op`** - 事件通道操作
- **用途**: 域间事件通信
- **权限**: Domain U 可以使用
- **功能**:
  - 分配事件通道
  - 绑定事件通道
  - 发送事件
  - 关闭事件通道
- **实现**: `xen/xen/common/event_channel.c` - `do_event_channel_op()`

**`HYPERVISOR_argo_op`** - Argo 域间通信
- **用途**: Argo 域间通信协议
- **权限**: Domain U 可以使用（需要策略允许）
- **功能**:
  - 发送消息到其他 Domain
  - 接收来自其他 Domain 的消息
  - 管理通信环
- **实现**: `xen/xen/common/argo.c` - `do_argo_op()`

**`HYPERVISOR_console_io`** - 控制台 I/O
- **用途**: 控制台输入/输出
- **权限**: Domain U 可以使用
- **功能**:
  - 读取控制台
  - 写入控制台
- **实现**: `xen/xen/common/console.c` - `do_console_io()`

#### 6.2.3 调度和 VCPU 类

**`HYPERVISOR_sched_op`** - 调度操作
- **用途**: CPU 调度控制
- **权限**: Domain U 可以使用
- **功能**:
  - 阻塞当前 VCPU
  - 让出 CPU
  - 设置调度参数
  - 获取调度信息
- **实现**: `xen/xen/common/sched/core.c` - `do_sched_op()`

**`HYPERVISOR_set_timer_op`** - 设置定时器
- **用途**: 设置 VCPU 定时器
- **权限**: Domain U 可以使用
- **功能**: 设置 VCPU 唤醒时间
- **实现**: `xen/xen/arch/x86/time.c` - `do_set_timer()`

**`HYPERVISOR_vcpu_op`** - VCPU 操作（部分操作）
- **用途**: VCPU 管理
- **权限**: 部分操作 Domain U 可以使用，部分需要特权
- **Domain U 可用操作**:
  - `VCPUOP_register_vcpu_info` - 注册 VCPU 信息
  - `VCPUOP_register_runstate_memory_area` - 注册运行状态内存区域
  - `VCPUOP_get_runstate_info` - 获取运行状态信息
  - `VCPUOP_set_periodic_timer` - 设置周期性定时器
  - `VCPUOP_stop_periodic_timer` - 停止周期性定时器
- **需要特权的操作**:
  - `VCPUOP_initialise` - 初始化 VCPU（通常由 Domain 0 调用）
  - `VCPUOP_up` - 启动 VCPU
  - `VCPUOP_down` - 停止 VCPU
  - `VCPUOP_is_up` - 检查 VCPU 是否运行
  - `VCPUOP_set_timer` - 设置定时器（某些场景）
  - `VCPUOP_get_timer` - 获取定时器
  - `VCPUOP_set_singleshot_timer` - 设置单次定时器
  - `VCPUOP_stop_singleshot_timer` - 停止单次定时器
  - `VCPUOP_set_isa_irq` - 设置 ISA IRQ
  - `VCPUOP_send_nmi` - 发送 NMI
  - `VCPUOP_get_physid` - 获取物理 ID
  - `VCPUOP_register_vcpu_time_memory_area` - 注册 VCPU 时间内存区域

#### 6.2.4 信息查询类

**`HYPERVISOR_xen_version`** - 获取 Xen 版本
- **用途**: 获取 Hypervisor 版本信息
- **权限**: Domain U 可以使用
- **功能**:
  - 获取版本号
  - 获取编译信息
  - 获取功能特性
- **实现**: `xen/xen/common/kernel.c` - `do_xen_version()`

**`HYPERVISOR_vm_assist`** - VM 辅助功能
- **用途**: 启用/禁用 VM 辅助功能
- **权限**: Domain U 可以使用
- **功能**:
  - 启用/禁用各种 VM 辅助功能
- **实现**: `xen/xen/common/kernel.c` - `do_vm_assist()`

#### 6.2.5 HVM 特定类

**`HYPERVISOR_hvm_op`** - HVM 操作（部分操作）
- **用途**: HVM Guest 特定操作
- **权限**: 部分操作 Domain U 可以使用
- **Domain U 可用操作**:
  - `HVMOP_get_param` - 获取 HVM 参数
  - `HVMOP_set_param` - 设置 HVM 参数（某些参数）
  - `HVMOP_guest_request_vm_event` - Guest 请求 VM 事件（可能允许用户空间）
- **需要特权的操作**:
  - `HVMOP_set_param` - 设置某些敏感参数
  - 其他管理操作
- **实现**: `xen/xen/arch/x86/hvm/hypercall.c` - `do_hvm_op()`

#### 6.2.6 其他

**`HYPERVISOR_multicall`** - 批量 Hypercall
- **用途**: 批量执行多个 hypercall
- **权限**: Domain U 可以使用
- **功能**: 在一个 hypercall 中执行多个 hypercall
- **实现**: `xen/xen/common/multicall.c` - `do_multicall()`

**`HYPERVISOR_dm_op`** - 设备模型操作
- **用途**: 设备模型操作（IOREQ 服务器等）
- **权限**: Domain U 可以使用（需要策略允许）
- **功能**:
  - 创建 IOREQ 服务器
  - 映射 I/O 范围
  - 管理设备模型状态
- **实现**: `xen/xen/common/dm.c` - `do_dm_op()`

**`HYPERVISOR_hypfs_op`** - Hypervisor 文件系统操作
- **用途**: 访问 Hypervisor 文件系统
- **权限**: Domain U 可以使用（只读访问）
- **功能**: 读取 Hypervisor 文件系统信息
- **实现**: `xen/xen/common/hypfs.c` - `do_hypfs_op()`

### 6.3 x86 PV 特定 Hypercall（Domain U 可用）

以下 hypercall 仅适用于 x86 PV Guest，Domain U 可以使用：

- **`HYPERVISOR_set_trap_table`** - 设置陷阱表
- **`HYPERVISOR_mmu_update`** - MMU 更新
- **`HYPERVISOR_set_gdt`** - 设置 GDT
- **`HYPERVISOR_stack_switch`** - 栈切换
- **`HYPERVISOR_set_callbacks`** - 设置回调
- **`HYPERVISOR_fpu_taskswitch`** - FPU 任务切换
- **`HYPERVISOR_update_va_mapping`** - 更新虚拟地址映射
- **`HYPERVISOR_update_descriptor`** - 更新描述符
- **`HYPERVISOR_set_debugreg`** - 设置调试寄存器
- **`HYPERVISOR_get_debugreg`** - 获取调试寄存器
- **`HYPERVISOR_iret`** - 中断返回
- **`HYPERVISOR_mmuext_op`** - MMU 扩展操作
- **`HYPERVISOR_set_segment_base`** - 设置段基址
- **`HYPERVISOR_mca`** - 机器检查架构

### 6.4 权限检查机制

#### 6.4.1 基本权限检查

**CPL 检查**（HVM）:
```102:130:xen/xen/arch/x86/hvm/hypercall.c
int hvm_hypercall(struct cpu_user_regs *regs)
{
    struct vcpu *curr = current;
    struct domain *currd = curr->domain;
    int mode = hvm_guest_x86_mode(curr);
    unsigned long eax = regs->eax;
    unsigned int token;

    switch ( mode )
    {
    case 8:
        eax = regs->rax;
        /* Fallthrough to permission check. */
    case 4:
    case 2:
        if ( currd->arch.monitor.guest_request_userspace_enabled &&
            eax == __HYPERVISOR_hvm_op &&
            (mode == 8 ? regs->rdi : regs->ebx) == HVMOP_guest_request_vm_event )
            break;

        if ( unlikely(hvm_get_cpl(curr)) )
        {
    default:
            regs->rax = -EPERM;
            return HVM_HCALL_completed;
        }
    case 0:
        break;
    }
```

**说明**: HVM Guest 必须在内核模式（CPL=0）才能调用 hypercall。

#### 6.4.2 硬件域检查

**`is_hardware_domain()`** 检查:
- 用于检查是否为硬件域（通常是 Domain 0）
- 某些操作（如 PCI 设备管理）需要硬件域权限

#### 6.4.3 XSM/FLASK 权限检查

**XSM (Xen Security Module)** 提供细粒度权限控制:
- `xsm_sysctl()` - sysctl 权限检查
- `xsm_domctl()` - domctl 权限检查
- `xsm_physdev_op()` - physdev_op 权限检查
- 其他 XSM hook

**FLASK 策略**:
- Domain 0 在 FLASK 策略中有特殊权限
- 参见 `xen/tools/flask/policy/modules/dom0.te`

### 6.5 权限总结表

| Hypercall | Domain 0 | Domain U | 说明 |
|-----------|----------|----------|------|
| `HYPERVISOR_sysctl` | ✅ | ❌ | 系统管理，仅 Domain 0 |
| `HYPERVISOR_domctl` | ✅ | ❌ | Domain 管理，仅 Domain 0 |
| `HYPERVISOR_physdev_op` | ✅ | ⚠️ | 部分操作需要硬件域 |
| `HYPERVISOR_platform_op` | ✅ | ⚠️ | 部分操作需要特权 |
| `HYPERVISOR_memory_op` | ✅ | ✅ | 大部分操作 Domain U 可用 |
| `HYPERVISOR_grant_table_op` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_event_channel_op` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_sched_op` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_vcpu_op` | ✅ | ⚠️ | 部分操作 Domain U 可用 |
| `HYPERVISOR_console_io` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_xen_version` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_hvm_op` | ✅ | ⚠️ | 部分操作 Domain U 可用 |
| `HYPERVISOR_argo_op` | ✅ | ✅ | Domain U 可用（需策略允许） |
| `HYPERVISOR_multicall` | ✅ | ✅ | Domain U 可用 |
| `HYPERVISOR_dm_op` | ✅ | ✅ | Domain U 可用（需策略允许） |
| `HYPERVISOR_hypfs_op` | ✅ | ✅ | Domain U 可用（只读） |

**图例**:
- ✅: 可以使用
- ❌: 不能使用
- ⚠️: 部分操作可用，部分需要特权

## 七、Hypercall 处理流程

### 7.1 完整处理流程概览

```
Guest 发起 Hypercall
    |
    v
架构特定入口（汇编）
    |
    v
架构特定处理（C 代码）
    |
    v
权限检查
    |
    v
Hypercall 分发（call_handlers_*）
    |
    v
调用处理函数（do_*）
    |
    v
执行操作
    |
    v
返回结果
```

### 7.2 Guest 发起 Hypercall

#### 7.2.1 x86 PV Guest

**64-bit PV**:
- **指令**: `SYSCALL`
- **入口**: `lstar_enter` (`xen/xen/arch/x86/x86_64/entry.S:255`)

```255:290:xen/xen/arch/x86/x86_64/entry.S
ENTRY(lstar_enter)
#ifdef CONFIG_XEN_SHSTK
        ALTERNATIVE "", "setssbsy", X86_FEATURE_XEN_SHSTK
#endif
        push  %rax          /* Guest %rsp */
        movq  8(%rsp), %rax /* Restore guest %rax */
        movq  $FLAT_KERNEL_SS,8(%rsp)
        pushq %r11
        pushq $FLAT_KERNEL_CS64
        pushq %rcx
        pushq $0
        movl  $TRAP_syscall, EFRAME_entry_vector(%rsp)
        SAVE_ALL

        GET_STACK_END(14)

        SPEC_CTRL_ENTRY_FROM_PV /* Req: %rsp=regs/cpuinfo, %r14=end, %rdx=0, Clob: abcd */
        /* WARNING! `ret`, `call *`, `jmp *` not safe before this point. */

        mov   STACK_CPUINFO_FIELD(xen_cr3)(%r14), %rcx
        test  %rcx, %rcx
        jz    .Llstar_cr3_okay
        movb  $0, STACK_CPUINFO_FIELD(use_pv_cr3)(%r14)
        mov   %rcx, %cr3
        /* %r12 is still zero at this point. */
        mov   %r12, STACK_CPUINFO_FIELD(xen_cr3)(%r14)
.Llstar_cr3_okay:
        sti

        movq  STACK_CPUINFO_FIELD(current_vcpu)(%r14), %rbx
        testb $TF_kernel_mode,VCPU_thread_flags(%rbx)
        jz    switch_to_kernel

        mov   %rsp, %rdi
        call  pv_hypercall
        jmp   test_all_events
```

**32-bit PV**:
- **指令**: `INT 0x82`
- **入口**: `do_entry_int82` (`xen/xen/arch/x86/pv/hypercall.c:182`)

**Hypercall Page**:
- Xen 在 Guest 内存中写入 hypercall stub
- Guest 通过 `call hypercall_page + index * 32` 调用
- 抽象了不同模式的差异

#### 7.2.2 x86 HVM Guest

**Intel HVM**:
- **指令**: `VMCALL`
- **VM Exit**: `EXIT_REASON_VMCALL` (`xen/xen/arch/x86/hvm/vmx/vmx.c:4560`)

```4560:4565:xen/xen/arch/x86/hvm/vmx/vmx.c
    case EXIT_REASON_VMCALL:
        HVMTRACE_1D(VMMCALL, regs->eax);

        if (hvm_hypercall(regs) == HVM_HCALL_completed)
            update_guest_eip(); /* Safe: VMCALL */
        break;
```

**AMD HVM**:
- **指令**: `VMMCALL`
- **VM Exit**: 类似处理

#### 7.2.3 ARM Guest

**ARM64**:
- **指令**: `HVC #XEN_HYPERCALL_TAG`
- **异常**: `HSR_EC_HVC64` (`xen/xen/arch/arm/traps.c:1394`)

```1394:1447:xen/xen/arch/arm/traps.c
static void do_trap_hypercall(struct cpu_user_regs *regs, register_t *nr,
                              const union hsr hsr)
{
    struct vcpu *curr = current;

    if ( hsr.iss != XEN_HYPERCALL_TAG )
    {
        gprintk(XENLOG_WARNING, "Invalid HVC imm 0x%x\n", hsr.iss);
        return inject_undef_exception(regs, hsr);
    }

    curr->hcall_preempted = false;

    perfc_incra(hypercalls, *nr);

    call_handlers_arm(*nr, HYPERCALL_RESULT_REG(regs), HYPERCALL_ARG1(regs),
                      HYPERCALL_ARG2(regs), HYPERCALL_ARG3(regs),
                      HYPERCALL_ARG4(regs), HYPERCALL_ARG5(regs));

#ifndef NDEBUG
    if ( !curr->hcall_preempted && HYPERCALL_RESULT_REG(regs) != -ENOSYS )
    {
        /* Deliberately corrupt parameter regs used by this hypercall. */
        switch ( hypercall_args[*nr] ) {
        case 5: HYPERCALL_ARG5(regs) = 0xDEADBEEFU;
        case 4: HYPERCALL_ARG4(regs) = 0xDEADBEEFU;
        case 3: HYPERCALL_ARG3(regs) = 0xDEADBEEFU;
        case 2: HYPERCALL_ARG2(regs) = 0xDEADBEEFU;
        case 1: /* Don't clobber x0/r0 -- it's the return value */
        case 0: /* -ENOSYS case */
            break;
        default: BUG();
        }
        *nr = 0xDEADBEEFU;
    }
#endif

    /* Ensure the hypercall trap instruction is re-executed. */
    if ( curr->hcall_preempted )
        regs->pc -= 4;  /* re-execute 'hvc #XEN_HYPERCALL_TAG' */

#ifdef CONFIG_IOREQ_SERVER
    /*
     * We call ioreq_signal_mapcache_invalidate from do_trap_hypercall()
     * because the only way a guest can modify its P2M on Arm is via an
     * hypercall.
     * Note that sending the invalidation request causes the vCPU to block
     * until all the IOREQ servers have acknowledged the invalidation.
     */
    if ( unlikely(curr->mapcache_invalidate) &&
         test_and_clear_bool(curr->mapcache_invalidate) )
        ioreq_signal_mapcache_invalidate();
#endif
}
```

### 7.3 架构特定处理

#### 7.3.1 x86 PV Hypercall 处理

**位置**: `xen/xen/arch/x86/pv/hypercall.c:19`

```19:86:xen/xen/arch/x86/pv/hypercall.c
static void always_inline
_pv_hypercall(struct cpu_user_regs *regs, bool compat)
{
    struct vcpu *curr = current;
    unsigned long eax;

    ASSERT(guest_kernel_mode(curr, regs));

    curr->hcall_preempted = false;

    if ( !compat )
    {
        unsigned long rdi = regs->rdi;
        unsigned long rsi = regs->rsi;
        unsigned long rdx = regs->rdx;
        unsigned long r10 = regs->r10;
        unsigned long r8 = regs->r8;

        eax = regs->rax;

        if ( unlikely(tb_init_done) )
        {
            unsigned long args[5] = { rdi, rsi, rdx, r10, r8 };

            __trace_hypercall(TRC_PV_HYPERCALL_V2, eax, args);
        }

        call_handlers_pv64(eax, regs->rax, rdi, rsi, rdx, r10, r8);

        if ( !curr->hcall_preempted && regs->rax != -ENOSYS )
            clobber_regs(regs, eax, pv, 64);
    }
#ifdef CONFIG_PV32
    else
    {
        unsigned int ebx = regs->ebx;
        unsigned int ecx = regs->ecx;
        unsigned int edx = regs->edx;
        unsigned int esi = regs->esi;
        unsigned int edi = regs->edi;

        eax = regs->eax;

        if ( unlikely(tb_init_done) )
        {
            unsigned long args[5] = { ebx, ecx, edx, esi, edi };

            __trace_hypercall(TRC_PV_HYPERCALL_V2, eax, args);
        }

        curr->hcall_compat = true;
        call_handlers_pv32(eax, regs->eax, ebx, ecx, edx, esi, edi);
        curr->hcall_compat = false;

        if ( !curr->hcall_preempted && regs->eax != -ENOSYS )
            clobber_regs(regs, eax, pv, 32);
    }
#endif /* CONFIG_PV32 */

    /*
     * PV guests use SYSCALL or INT $0x82 to make a hypercall, both of which
     * have trap semantics.  If the hypercall has been preempted, rewind the
     * instruction pointer to reexecute the instruction.
     */
    if ( curr->hcall_preempted )
        regs->rip -= 2;

    perfc_incra(hypercalls, eax);
}
```

**关键步骤**:
1. **权限检查**: `ASSERT(guest_kernel_mode(curr, regs))` - 确保在内核模式
2. **重置标志**: `curr->hcall_preempted = false`
3. **提取参数**: 从寄存器提取 hypercall 编号和参数
4. **跟踪**: 如果启用跟踪，记录 hypercall
5. **分发**: 调用 `call_handlers_pv64()` 或 `call_handlers_pv32()`
6. **参数清理**: 如果未抢占，清理参数寄存器（调试构建）
7. **抢占处理**: 如果被抢占，回退指令指针

#### 7.3.2 x86 HVM Hypercall 处理

**位置**: `xen/xen/arch/x86/hvm/hypercall.c:102`

```102:192:xen/xen/arch/x86/hvm/hypercall.c
int hvm_hypercall(struct cpu_user_regs *regs)
{
    struct vcpu *curr = current;
    struct domain *currd = curr->domain;
    int mode = hvm_guest_x86_mode(curr);
    unsigned long eax = regs->eax;
    unsigned int token;

    switch ( mode )
    {
    case 8:
        eax = regs->rax;
        /* Fallthrough to permission check. */
    case 4:
    case 2:
        if ( currd->arch.monitor.guest_request_userspace_enabled &&
            eax == __HYPERVISOR_hvm_op &&
            (mode == 8 ? regs->rdi : regs->ebx) == HVMOP_guest_request_vm_event )
            break;

        if ( unlikely(hvm_get_cpl(curr)) )
        {
    default:
            regs->rax = -EPERM;
            return HVM_HCALL_completed;
        }
    case 0:
        break;
    }

    if ( (eax & 0x80000000U) && is_viridian_domain(currd) )
    {
        int ret;

        /* See comment below. */
        token = hvmemul_cache_disable(curr);

        ret = viridian_hypercall(regs);

        hvmemul_cache_restore(curr, token);

        return ret;
    }

    /*
     * Caching is intended for instruction emulation only. Disable it
     * for any accesses by hypercall argument copy-in / copy-out.
     */
    token = hvmemul_cache_disable(curr);

    curr->hcall_preempted = false;

    if ( mode == 8 )
    {
        HVM_DBG_LOG(DBG_LEVEL_HCALL, "hcall%lu(%lx, %lx, %lx, %lx, %lx)",
                    eax, regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8);

        call_handlers_hvm64(eax, regs->rax, regs->rdi, regs->rsi, regs->rdx,
                            regs->r10, regs->r8);

        if ( !curr->hcall_preempted && regs->rax != -ENOSYS )
            clobber_regs(regs, eax, hvm, 64);
    }
    else
    {
        HVM_DBG_LOG(DBG_LEVEL_HCALL, "hcall%lu(%x, %x, %x, %x, %x)", eax,
                    regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi);

        curr->hcall_compat = true;
        call_handlers_hvm32(eax, regs->eax, regs->ebx, regs->ecx, regs->edx,
                            regs->esi, regs->edi);
        curr->hcall_compat = false;

        if ( !curr->hcall_preempted && regs->eax != -ENOSYS )
            clobber_regs(regs, eax, hvm, 32);
    }

    hvmemul_cache_restore(curr, token);

    HVM_DBG_LOG(DBG_LEVEL_HCALL, "hcall%lu -> %lx", eax, regs->rax);

    if ( unlikely(curr->mapcache_invalidate) )
    {
        curr->mapcache_invalidate = false;
        ioreq_signal_mapcache_invalidate();
    }

    perfc_incra(hypercalls, eax);

    return curr->hcall_preempted ? HVM_HCALL_preempted : HVM_HCALL_completed;
}
```

**关键步骤**:
1. **模式检测**: 检测 Guest 运行模式（64-bit/32-bit/16-bit）
2. **权限检查**: 检查 CPL（Current Privilege Level），必须在内核模式（CPL=0）
3. **Viridian 检查**: 检查是否为 Viridian（Hyper-V）hypercall
4. **禁用缓存**: 禁用指令模拟缓存（用于参数复制）
5. **分发**: 调用 `call_handlers_hvm64()` 或 `call_handlers_hvm32()`
6. **恢复缓存**: 恢复指令模拟缓存
7. **Mapcache 失效**: 如果 mapcache 需要失效，发送信号

### 7.4 Hypercall 分发机制

#### 7.4.1 分发函数

Xen 使用自动生成的 `call_handlers_*` 函数进行分发：

- **`call_handlers_pv64()`**: x86 64-bit PV
- **`call_handlers_pv32()`**: x86 32-bit PV
- **`call_handlers_hvm64()`**: x86 64-bit HVM
- **`call_handlers_hvm32()`**: x86 32-bit HVM
- **`call_handlers_arm()`**: ARM

这些函数由 `scripts/gen_hypercall.awk` 脚本从 `hypercall-defs.c` 生成。

#### 7.4.2 路由表

**位置**: `xen/xen/include/hypercall-defs.c`

路由表定义了每个 hypercall 在不同架构和模式下的处理函数：

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
```

**说明**:
- **`do`**: 调用 `do_*` 函数
- **`compat`**: 调用 `compat_*` 函数（兼容模式）
- **`hvm`**: 调用 `hvm_*` 函数
- **`dep`**: 已弃用
- **`-`**: 不支持
- **`:1`, `:2`**: 优先级（数字越小优先级越高）

#### 7.4.3 处理函数命名

处理函数遵循以下命名规则：

- **PV**: `do_<hypercall_name>()`
- **HVM**: `hvm_<hypercall_name>()` 或 `do_<hypercall_name>()`
- **兼容**: `compat_<hypercall_name>()`
- **ARM**: `do_arm_<hypercall_name>()` 或 `do_<hypercall_name>()`

### 7.5 权限检查

#### 7.5.1 基本权限检查

**PV Guest**:
- **内核模式**: `ASSERT(guest_kernel_mode(curr, regs))`
- 必须在 Guest 内核模式才能调用 hypercall

**HVM Guest**:
- **CPL 检查**: `hvm_get_cpl(curr) == 0`
- 必须在 CPL=0（内核模式）才能调用 hypercall

**ARM Guest**:
- **异常级别**: 必须在 EL1（内核模式）

#### 7.5.2 特权检查

某些 hypercall 需要特权域（Domain 0）权限：

- **`HYPERVISOR_sysctl`**: 系统管理操作
- **`HYPERVISOR_domctl`**: Domain 管理操作
- **`HYPERVISOR_physdev_op`**: 部分物理设备操作

这些检查在处理函数内部进行。

#### 7.5.3 XSM/FLASK 检查

XSM (Xen Security Module) 提供细粒度权限控制：

- **`xsm_sysctl()`**: sysctl 权限检查
- **`xsm_domctl()`**: domctl 权限检查
- **`xsm_physdev_op()`**: physdev_op 权限检查

### 7.6 处理函数执行

#### 7.6.1 参数提取

参数从寄存器中提取：

**x86 64-bit**:
- **参数 1-5**: RDI, RSI, RDX, R10, R8
- **返回值**: RAX

**x86 32-bit**:
- **参数 1-5**: EBX, ECX, EDX, ESI, EDI
- **返回值**: EAX

**ARM64**:
- **参数 1-5**: X0, X1, X2, X3, X4
- **返回值**: X0

**ARM32**:
- **参数 1-5**: R0, R1, R2, R3, R4
- **返回值**: R0

#### 7.6.2 Guest 内存访问

Hypercall 参数可能包含指向 Guest 内存的指针：

- **`XEN_GUEST_HANDLE_PARAM()`**: Guest 句柄类型
- **`copy_from_guest()`**: 从 Guest 内存复制数据
- **`copy_to_guest()`**: 向 Guest 内存复制数据
- **`guest_handle_is_null()`**: 检查 Guest 句柄是否为空

#### 7.6.3 错误处理

- **返回值**: 负数表示错误码（如 `-EINVAL`, `-EPERM`）
- **成功**: 返回 0 或正数
- **`-ENOSYS`**: Hypercall 未实现或未支持

### 7.7 抢占和延续

#### 7.7.1 Hypercall 抢占

某些 hypercall 可能被抢占（preempted）：

- **`curr->hcall_preempted = true`**: 标记 hypercall 被抢占
- **指令回退**: 回退指令指针，重新执行 hypercall 指令
- **延续**: 使用 `hypercall_create_continuation()` 创建延续

#### 7.7.2 延续机制

**位置**: `xen/xen/arch/x86/hypercall.c:26`

```26:99:xen/xen/arch/x86/hypercall.c
unsigned long hypercall_create_continuation(
    unsigned int op, const char *format, ...)
{
    struct vcpu *curr = current;
    struct mc_state *mcs = &curr->mc_state;
    const char *p = format;
    unsigned long arg;
    unsigned int i;
    va_list args;

    curr->hcall_preempted = true;

    va_start(args, format);

    if ( mcs->flags & MCSF_in_multicall )
    {
        for ( i = 0; *p != '\0'; i++ )
            mcs->call.args[i] = NEXT_ARG(p, args);
    }
    else
    {
        struct cpu_user_regs *regs = guest_cpu_user_regs();

        regs->rax = op;

#ifdef CONFIG_COMPAT
        if ( !curr->hcall_compat )
#else
        if ( true )
#endif
        {
            for ( i = 0; *p != '\0'; i++ )
            {
                arg = NEXT_ARG(p, args);
                switch ( i )
                {
                case 0: regs->rdi = arg; break;
                case 1: regs->rsi = arg; break;
                case 2: regs->rdx = arg; break;
                case 3: regs->r10 = arg; break;
                case 4: regs->r8  = arg; break;
                case 5: regs->r9  = arg; break;
                }
            }
        }
        else
        {
            for ( i = 0; *p != '\0'; i++ )
            {
                arg = NEXT_ARG(p, args);
                switch ( i )
                {
                case 0: regs->rbx = arg; break;
                case 1: regs->rcx = arg; break;
                case 2: regs->rdx = arg; break;
                case 3: regs->rsi = arg; break;
                case 4: regs->rdi = arg; break;
                case 5: regs->rbp = arg; break;
                }
            }
        }
    }

    va_end(args);

    return op;
```

**用途**: 当 hypercall 需要分多次执行时（如处理大量页面），创建延续以便后续继续执行。

### 7.8 返回机制

#### 7.8.1 返回值设置

返回值通过寄存器返回：

- **x86**: `regs->rax` (64-bit) 或 `regs->eax` (32-bit)
- **ARM**: `HYPERCALL_RESULT_REG(regs)` (X0/R0)

#### 7.8.2 参数清理（调试构建）

在调试构建中，参数寄存器会被清理（设置为 `0xDEADBEEF`），防止 Guest 依赖参数寄存器保持不变。

#### 7.8.3 返回 Guest

**PV Guest**:
- 通过 `test_all_events` 检查事件
- 恢复 Guest 上下文
- 返回 Guest

**HVM Guest**:
- 更新 Guest EIP（指令指针）
- 返回 Guest

**ARM Guest**:
- 如果被抢占，回退 PC（程序计数器）
- 返回 Guest

### 7.9 完整流程示例

#### 7.9.1 x86 PV Guest 调用 `HYPERVISOR_memory_op`

```
1. Guest 准备参数
   - RAX = __HYPERVISOR_memory_op
   - RDI = cmd
   - RSI = arg (指向 Guest 内存)

2. Guest 执行 SYSCALL
   - 触发 VM Exit（PV 使用 trap）

3. 进入 lstar_enter (entry.S)
   - 保存 Guest 上下文
   - 设置 Xen 栈
   - 调用 pv_hypercall()

4. pv_hypercall() (hypercall.c)
   - 检查内核模式
   - 提取参数
   - 调用 call_handlers_pv64()

5. call_handlers_pv64() (自动生成)
   - 根据 hypercall 编号路由
   - 调用 do_memory_op()

6. do_memory_op() (memory.c)
   - 权限检查
   - 从 Guest 内存复制参数
   - 执行操作
   - 返回结果

7. 返回 Guest
   - 设置 RAX = 返回值
   - 清理参数寄存器（调试构建）
   - 恢复 Guest 上下文
   - 返回 Guest
```

#### 7.9.2 x86 HVM Guest 调用 `HYPERVISOR_hvm_op`

```
1. Guest 准备参数
   - RAX = __HYPERVISOR_hvm_op
   - RDI = cmd
   - RSI = arg

2. Guest 执行 VMCALL
   - 触发 VM Exit (EXIT_REASON_VMCALL)

3. VMX 处理 (vmx.c)
   - 调用 hvm_hypercall()

4. hvm_hypercall() (hypercall.c)
   - 检查 CPL (必须为 0)
   - 禁用模拟缓存
   - 调用 call_handlers_hvm64()

5. call_handlers_hvm64() (自动生成)
   - 路由到 hvm_hvm_op()

6. hvm_hvm_op() (hvm.c)
   - 执行 HVM 特定操作
   - 返回结果

7. 返回 Guest
   - 更新 Guest EIP
   - 恢复模拟缓存
   - 返回 Guest
```

### 7.10 关键机制总结

#### 7.10.1 权限检查层次

1. **架构层**: 内核模式检查（PV/HVM/ARM）
2. **Hypercall 层**: 特权域检查（某些 hypercall）
3. **XSM 层**: 细粒度权限检查（FLASK）

#### 7.10.2 参数处理

1. **提取**: 从寄存器提取参数
2. **验证**: 验证参数有效性
3. **复制**: 从 Guest 内存复制数据（如果需要）
4. **执行**: 执行操作
5. **返回**: 将结果写回 Guest 内存（如果需要）

#### 7.10.3 错误处理

1. **参数验证**: 检查参数有效性
2. **权限检查**: 检查调用权限
3. **操作执行**: 执行操作
4. **错误返回**: 返回错误码（负数）

#### 7.10.4 性能优化

1. **跟踪**: 可选跟踪支持
2. **缓存**: HVM 禁用模拟缓存以提高性能
3. **优先级**: 路由表支持优先级
4. **抢占**: 支持抢占和延续

## 八、参考

- `xen/xen/include/public/xen.h` - Hypercall 编号定义
- `xen/xen/include/hypercall-defs.c` - Hypercall 定义和路由
- `xen/xen/arch/x86/pv/hypercall.c` - x86 PV hypercall 处理
- `xen/xen/arch/x86/hvm/hypercall.c` - x86 HVM hypercall 处理
- `xen/xen/arch/arm/traps.c` - ARM hypercall 处理
- `xen/xen/arch/x86/x86_64/entry.S` - x86 64-bit 入口
- `xen/docs/guest-guide/x86/hypercall-abi.rst` - Hypercall ABI 文档
- `xen/xen/include/public/arch-arm.h` - ARM Hypercall 说明
- [Xen Project Wiki - Hypercalls](https://wiki.xenproject.org/wiki/Hypercall)
