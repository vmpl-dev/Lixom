# Xen Hypervisor 启动流程

**日期**: 2026-01-01
**相关文件**: `xen/xen/arch/x86/setup.c`

## 核心内容

### 主入口点

Xen Hypervisor 的 x86 架构主入口点是 `__start_xen()` 函数：

```c
void __init noreturn __start_xen(unsigned long mbi_p)
{
    // 初始化流程
    // ...
}
```

**位置**: `xen/xen/arch/x86/setup.c:972`

### 关键步骤

1. **初始化阶段**
   - `percpu_init_areas()` - 初始化每 CPU 区域
   - `init_idt_traps()` - 初始化中断描述符表
   - `load_system_tables()` - 加载系统表

2. **内存管理**
   - 解析内存映射
   - 初始化页表

3. **Domain 0 创建**
   - 调用 `create_dom0()` 创建特权域

## 理解总结

- Xen 是一个 bare metal hypervisor，直接运行在硬件上
- 启动时首先初始化自身环境，然后创建 Domain 0
- Domain 0 是第一个虚拟机，负责管理其他域

## 疑问

- [ ] 为什么需要 `noreturn` 属性？
- [ ] 多核启动时如何同步？

## 参考链接

- [Xen 官方文档](https://wiki.xenproject.org/)
- `xen/xen/arch/x86/setup.c`
