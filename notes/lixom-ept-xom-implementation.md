# Lixom EPT-XOM 实现分析

**日期**: 2026-01-01
**相关文件**:
- `xen/xen/arch/x86/xom_seal.c` - Hypervisor 层 XOM 实现
- `xen/xen/include/xen/xom_seal.h` - XOM 接口定义
- `modxom/modxom.c` - Linux 内核模块
- `libxom/libxom.c` - 用户空间库
- `libxom/xom.h` - 用户空间 API

## 概述

**Lixom** 是一个利用 **EPT-XOM (Extended Page Tables - Execute-Only Memory)** 技术保护应用程序敏感数据（特别是加密密钥）的研究项目。它通过在 Hypervisor 层设置 EPT 页表权限，实现硬件级别的执行保护，防止敏感代码和数据被读取或写入。

### 核心特性

1. **EPT 级别的 Execute-Only 保护**: 利用 Intel EPT 硬件特性，在 Hypervisor 层设置页表权限
2. **寄存器清除机制**: 在中断/异常处理时清除寄存器，防止侧信道攻击
3. **子页（Subpage）机制**: 支持 128 字节粒度的细粒度保护
4. **双模式支持**:
   - **Lixom (EPT)**: 使用 EPT 实现，需要修改的 Xen Hypervisor
   - **Lixom-Light (MPK)**: 使用 Memory Protection Keys，无需 Hypervisor 修改

## 一、架构概览

### 1.1 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                   应用程序层                              │
│  (OpenSSL Provider, 用户程序)                            │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ libxom API
                     ▼
┌─────────────────────────────────────────────────────────┐
│                  libxom (用户空间库)                      │
│  - xom_alloc() / xom_lock()                              │
│  - 子页管理                                               │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ /proc/xom (ioctl)
                     ▼
┌─────────────────────────────────────────────────────────┐
│              modxom (Linux 内核模块)                      │
│  - 内存映射管理                                           │
│  - Hypercall 转发                                         │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ HYPERVISOR_mmuext_op
                     ▼
┌─────────────────────────────────────────────────────────┐
│            Xen Hypervisor (修改版)                        │
│  - EPT 页表权限设置 (p2m_access_x)                        │
│  - 寄存器清除机制                                         │
│  - 子页写入控制                                           │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ EPT 硬件
                     ▼
┌─────────────────────────────────────────────────────────┐
│                  Intel CPU (EPT)                          │
│  - Execute-Only 权限执行                                  │
└─────────────────────────────────────────────────────────┘
```

### 1.2 关键组件

| 组件 | 功能 | 文件位置 |
|------|------|----------|
| **xom_seal.c** | Hypervisor 层 XOM 实现，EPT 权限管理 | `xen/xen/arch/x86/xom_seal.c` |
| **modxom.c** | Linux 内核模块，提供 `/proc/xom` 接口 | `modxom/modxom.c` |
| **libxom.c** | 用户空间库，提供易用 API | `libxom/libxom.c` |
| **xom.h** | 用户空间 API 定义 | `libxom/xom.h` |

## 二、Hypervisor 层实现

### 2.1 EPT 权限设置

**核心函数**: `set_xom_seal()`

```116:146:xen/xen/arch/x86/xom_seal.c
static int set_xom_seal(struct domain* d, gfn_t gfn, unsigned int nr_pages){
    int ret = 0;
    unsigned int i;
    struct p2m_domain *p2m;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING, "Entered set_xom_seal with gfn 0x%lx for %u pages. Max mapped page is 0x%lx\n", gfn_x(gfn) , nr_pages, p2m->max_mapped_pfn);

    if (!nr_pages)
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > p2m->max_mapped_pfn )
        return -EOVERFLOW;

    for ( i = 0; i < nr_pages; i++) {
        c_gfn = _gfn(gfn_x(gfn) + i);
        gfn_lock(p2m, c_gfn, 0);
        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_x, c_gfn);
        gfn_unlock(p2m, c_gfn, 0);
        if (ret < 0)
            break;
    }

    p2m->tlb_flush(p2m);
    return ret;
}
```

**关键步骤**:
1. **获取 P2M 域**: `p2m_get_hostp2m(d)` - 获取域的物理到机器地址映射
2. **锁定 GFN**: `gfn_lock()` - 防止并发访问
3. **设置 EPT 权限**: `p2m_set_mem_access_single(..., p2m_access_x, ...)` - 设置为 Execute-Only
4. **刷新 TLB**: `p2m->tlb_flush()` - 使新的权限生效

**EPT 权限类型** (`p2m_access_t`):
- `p2m_access_x` (4): Execute-Only - 只能执行，不能读写
- `p2m_access_rwx` (7): Read-Write-Execute - 完全访问
- `p2m_access_n` (0): No Access - 无访问权限

### 2.2 EPT 权限清除

**核心函数**: `clear_xom_seal()`

```148:221:xen/xen/arch/x86/xom_seal.c
static int clear_xom_seal(struct domain* d, gfn_t gfn, unsigned int nr_pages){
    int ret = 0;
    unsigned int i;
    void* xom_page;
    struct p2m_domain *p2m;
    struct page_info *page;
    xom_page_info* page_info;
    p2m_type_t ptype;
    p2m_access_t atype;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING, "Entered clear_xom_seal with gfn 0x%lx for %u pages. Max mapped page is 0x%lx\n", gfn_x(gfn) , nr_pages, p2m->max_mapped_pfn);

    if (!nr_pages)
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > p2m->max_mapped_pfn )
        return -EOVERFLOW;


    for ( i = 0; i < nr_pages; i++ ) {
        c_gfn = _gfn(gfn_x(gfn) + i);

        gfn_lock(p2m, c_gfn, 0);
        // Check whether the provided gfn is actually an XOM page
        p2m->get_entry(p2m, c_gfn, &ptype, &atype, 0, NULL, NULL);
        if (atype != p2m_access_x){
            gfn_unlock(p2m, c_gfn, 0);
            continue;
        }

        // Map the page into our address space
        page = get_page_from_gfn(d, gfn_x(c_gfn), NULL, P2M_ALLOC);

        if (!page) {
            ret = -EINVAL;
            gfn_unlock(p2m, c_gfn, 0);
            goto exit;
        }

        if (!get_page_type(page, PGT_writable_page)) {
            put_page(page);
            gfn_unlock(p2m, c_gfn, 0);
            ret = -EPERM;
            goto exit;
        }

        // Overwrite XOM page with 0x90
        xom_page = __map_domain_page(page);
        memset(xom_page, 0x90, PAGE_SIZE);
        unmap_domain_page(xom_page);
        put_page_and_type(page);

        // Set SLAT permissions to RWX
        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_rwx, c_gfn);
        gfn_unlock(p2m, c_gfn, 0);

        page_info = get_page_info_entry(&d->xom_subpages, c_gfn);
        if(page_info)
            rm_page_info_entry(&d->xom_subpages, page_info);
        page_info = get_page_info_entry(&d->xom_reg_clear_pages, c_gfn);
        if (page_info)
            rm_page_info_entry(&d->xom_reg_clear_pages, page_info);
    }

exit:
    p2m->tlb_flush(p2m);
    return ret;
}
```

**关键步骤**:
1. **验证 XOM 页**: 检查是否为 Execute-Only 页
2. **映射到 Hypervisor**: `__map_domain_page()` - 将 Guest 页映射到 Hypervisor 地址空间
3. **清除内容**: `memset(xom_page, 0x90, PAGE_SIZE)` - 用 NOP 指令（0x90）覆盖
4. **恢复权限**: `p2m_set_mem_access_single(..., p2m_access_rwx, ...)` - 恢复为 RWX
5. **清理元数据**: 删除子页和寄存器清除标记

### 2.3 子页（Subpage）机制

**目的**: 支持 128 字节粒度的细粒度保护，允许逐步写入 XOM 页。

**子页大小**: `SUBPAGE_SIZE = PAGE_SIZE / 32 = 4096 / 32 = 128 字节`

**创建子页**:

```223:272:xen/xen/arch/x86/xom_seal.c
static int create_xom_subpages(struct domain* d, gfn_t gfn, unsigned int nr_pages){
    int ret = 0;
    unsigned int i;
    struct p2m_domain *p2m;
    xom_page_info* subpage_info = NULL;
    p2m_type_t ptype;
    p2m_access_t atype;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING, "Entered create_subpages with gfn 0x%lx for %u (4KB!) pages. Max mapped page is 0x%lx\n", gfn_x(gfn) , nr_pages, p2m->max_mapped_pfn);

    if (!nr_pages)
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > p2m->max_mapped_pfn )
        return -EOVERFLOW;

    for ( i = 0; i < nr_pages; i++) {
        c_gfn = _gfn(gfn_x(gfn) + i);

        gfn_lock(p2m, c_gfn, 0);
        // Check whether the provided gfn is a XOM page already
        p2m->get_entry(p2m, c_gfn, &ptype, &atype, 0, NULL, NULL);
        if (atype == p2m_access_x){
            gfn_unlock(p2m, c_gfn, 0);
            ret = -EINVAL;
            goto exit;
        }

        // Set SLAT permissions to X
        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_x, c_gfn);
        gfn_unlock(p2m, c_gfn, 0);


        subpage_info = add_page_info_entry(&d->xom_subpages, c_gfn, (info_spec_t) {.lock_status = 0});
        if (!subpage_info) {
            ret = -ENOMEM;
            goto exit;
        }
    }

exit:
    p2m->tlb_flush(p2m);
    return ret;
}
```

**写入子页**:

```274:342:xen/xen/arch/x86/xom_seal.c
static int write_into_subpage(struct domain* d, gfn_t gfn_dest, gfn_t gfn_src){
    unsigned int i;
    char* xom_page, *write_dest;
    struct p2m_domain *p2m;
    struct page_info *page;
    xom_page_info* subpage_info;
    xom_subpage_write_command command;

    subpage_info = get_page_info_entry(&d->xom_subpages, gfn_dest);
    if(!subpage_info)
        return -EINVAL;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    if (gfn_x(gfn_src) > p2m->max_mapped_pfn )
        return -EOVERFLOW;

    // Copy command from gfn_src
    gfn_lock(p2m, gfn_src, 0);
    page = get_page_from_gfn(d, gfn_x(gfn_src), NULL, P2M_ALLOC);
    if(!page){
        gfn_unlock(p2m, gfn_src, 0);
        return -EINVAL;
    }
    xom_page = (char*) __map_domain_page(page);
    memcpy(&command, xom_page, sizeof(command));
    unmap_domain_page(xom_page);
    gfn_unlock(p2m, gfn_src, 0);
    put_page(page);

    gdprintk(XENLOG_WARNING, "Copying %u subpages from %lx to %lx\n", command.num_subpages, gfn_x(gfn_src), gfn_x(gfn_dest));

    // Validate command
    if(command.num_subpages > MAX_SUBPAGES_PER_CMD)
        return -EINVAL;
    for(i = 0; i < command.num_subpages; i++){
        if(command.write_info[i].target_subpage >= (PAGE_SIZE / SUBPAGE_SIZE))
            return -EINVAL;
        if(subpage_info->info.lock_status & (1 << command.write_info[i].target_subpage))
            return -EINVAL;
    }

    // Execute command
    gfn_lock(p2m, gfn_dest, 0);
    page = get_page_from_gfn(d, gfn_x(gfn_dest), NULL, P2M_ALLOC);
    if(!page){
        gfn_unlock(p2m, gfn_dest, 0);
        return -EINVAL;
    }
    if (!get_page_type(page, PGT_writable_page)) {
        put_page(page);
        gfn_unlock(p2m, gfn_dest, 0);
        return -EPERM;
    }
    xom_page = (char*) __map_domain_page(page);
    for(i = 0; i < command.num_subpages; i++){
        write_dest = xom_page + (command.write_info[i].target_subpage * SUBPAGE_SIZE);
        memcpy(write_dest, command.write_info[i].data, SUBPAGE_SIZE);
        subpage_info->info.lock_status |= 1 << command.write_info[i].target_subpage;
    }
    unmap_domain_page(xom_page);
    gfn_unlock(p2m, gfn_dest, 0);
    put_page_and_type(page);

    return 0;
}
```

**子页机制特点**:
- ✅ **细粒度控制**: 128 字节子页可以独立锁定
- ✅ **逐步写入**: 允许分多次写入一个 4KB 页
- ✅ **锁定状态跟踪**: 使用位图跟踪哪些子页已锁定
- ✅ **一次性写入**: 每个子页只能写入一次

### 2.4 寄存器清除机制

**目的**: 在中断/异常处理时清除寄存器，防止侧信道攻击泄露敏感信息。

**寄存器清除类型**:

```1:6:xen/xen/include/xen/xom_seal.h
#define REG_CLEAR_TYPE_NONE     0
#define REG_CLEAR_TYPE_VECTOR   1
#define REG_CLEAR_TYPE_FULL     2
```

- **REG_CLEAR_TYPE_NONE**: 不清除寄存器
- **REG_CLEAR_TYPE_VECTOR**: 清除 R15 和 SSE/AVX 寄存器
- **REG_CLEAR_TYPE_FULL**: 清除所有寄存器（除 RBP 和 RSP）

**标记寄存器清除页**:

```344:372:xen/xen/arch/x86/xom_seal.c
static int mark_reg_clear_page(struct domain* d, gfn_t gfn, unsigned int reg_clear_type) {
    xom_page_info* page_info;
    struct p2m_domain *p2m;
    p2m_type_t ptype;
    p2m_access_t atype;

    if(!reg_clear_type || reg_clear_type > REG_CLEAR_TYPE_FULL)
        return -EINVAL;

    // A page cannot be marked twice
    page_info = get_page_info_entry(&d->xom_reg_clear_pages, gfn);
    if(page_info)
        return -EINVAL;

    // We only allow marking XOM pages
    p2m = p2m_get_hostp2m(d);
    gfn_lock(p2m, c_gfn, 0);
    p2m->get_entry(p2m, gfn, &ptype, &atype, 0, NULL, NULL);
    gfn_unlock(p2m, c_gfn, 0);
    if (atype != p2m_access_x)
        return -EINVAL;


    page_info = add_page_info_entry(&d->xom_reg_clear_pages, gfn, (info_spec_t){.reg_clear_type = reg_clear_type});
    if (!page_info)
        return -ENOMEM;

    return 0;
}
```

**获取寄存器清除类型**:

```471:496:xen/xen/arch/x86/xom_seal.c
unsigned char get_reg_clear_type(const struct cpu_user_regs* const regs) {
    unsigned char ret = REG_CLEAR_TYPE_NONE;
    xom_page_info* info;
    gfn_t instr_gfn;
    struct vcpu* v = current;
    struct domain * const d = v->domain;

    if(!regs || !~(uintptr_t)regs)
        return ret;

    if (!is_hvm_domain(d) || !hap_enabled(d))
        return ret;

    instr_gfn = _gfn(gfn_of_rip(regs->rip));
    if ( unlikely(gfn_eq(instr_gfn, INVALID_GFN)) )
        return ret;

    spin_lock(&d->xom_page_lock);

    info = get_page_info_entry(&d->xom_reg_clear_pages, instr_gfn);
    if(info)
        ret = info->info.reg_clear_type;

    spin_unlock(&d->xom_page_lock);
    return ret;
}
```

**工作原理**:
1. 在 VM Exit 时，Hypervisor 检查当前执行的指令所在页（通过 `gfn_of_rip()`）
2. 如果该页标记了寄存器清除，则根据类型清除相应寄存器
3. 防止敏感数据通过寄存器泄露

**调用位置**: 在 `vmx_vmexit_handler()` 中调用

```4079:4116:xen/xen/arch/x86/hvm/vmx/vmx.c
static void handle_register_clear(struct cpu_user_regs *regs)
{
    unsigned char reg_clear_type;
    struct
    {
        uintptr_t sp;
        uintptr_t bp;
    } reg_backup;

    // We leave the kernel alone
    if (!(hvm_get_cpl(current) & 2))
        return;
    reg_clear_type = get_reg_clear_type(regs);

    if (reg_clear_type == REG_CLEAR_TYPE_NONE)
        return;

    if (cpu_has_avx512f)
        clear_avx512_regs();
    else if (cpu_has_avx)
        clear_avx_regs();
    else if (cpu_has_sse3)
        clear_sse_regs();

    if (reg_clear_type == REG_CLEAR_TYPE_VECTOR)
    {
        regs->r15 = regs->r14 = 0xbabababababababaull;
        return;
    }

    reg_backup.sp = regs->rsp;
    reg_backup.bp = regs->rbp;

    memset(regs, 0, sizeof(*regs));
    regs->rsp = reg_backup.sp;
    regs->rbp = reg_backup.bp;
    regs->r15 = 0xdadadadadadadadaull;
}
```

**清除策略**:
- **VECTOR 模式**: 清除 R14、R15 和所有 SIMD 寄存器（SSE/AVX/AVX512）
- **FULL 模式**: 清除所有寄存器，但保留 RSP 和 RBP（栈指针和帧指针）
- **保护内核**: 只在用户模式（CPL=3）时清除，内核模式不处理

### 2.5 Hypercall 接口

**处理函数**: `handle_xom_seal()`

```374:425:xen/xen/arch/x86/xom_seal.c
int handle_xom_seal(struct vcpu* curr,
                    XEN_GUEST_HANDLE_PARAM(mmuext_op_t) uops, unsigned int count, XEN_GUEST_HANDLE_PARAM(uint) pdone) {
    int rc;
    unsigned int i;
    struct domain* d = curr->domain;
    struct mmuext_op op;

    if (!is_hvm_domain(d) || !hap_enabled(d))
        return -EOPNOTSUPP;

    for ( i = 0; i < count; i++ ) {
        if (curr->arch.old_guest_table || (i && hypercall_preempt_check())) {
            gdprintk(XENLOG_ERR, "Preempt check failed\n");
            return -ERESTART;
        }

        if (unlikely(__copy_from_guest(&op, uops, 1) != 0)) {
            gdprintk(XENLOG_ERR, "Unable to copy guest page\n");
            return -EFAULT;
        }

        spin_lock(&d->xom_page_lock);
        switch (op.cmd){
            case MMUEXT_MARK_XOM:
                rc = set_xom_seal(d, _gfn(op.arg1.mfn), op.arg2.nr_ents);
                break;
            case MMUEXT_UNMARK_XOM:
                rc = clear_xom_seal(d, _gfn(op.arg1.mfn), op.arg2.nr_ents);
                break;
            case MMUEXT_CREATE_XOM_SPAGES:
                rc = create_xom_subpages(d, _gfn(op.arg1.mfn), op.arg2.nr_ents);
                break;
            case MMUEXT_WRITE_XOM_SPAGES:
                rc = write_into_subpage(d, _gfn(op.arg1.mfn), _gfn(op.arg2.src_mfn));
                break;
            case MMUEXT_MARK_REG_CLEAR:
                rc =  mark_reg_clear_page(d, _gfn(op.arg1.mfn), op.arg2.nr_ents);
                break;
            default:
                rc = -EOPNOTSUPP;
        }
        spin_unlock(&d->xom_page_lock);

        guest_handle_add_offset(uops, 1);
        if (rc < 0)
            return rc;
    }

    if ( unlikely(!guest_handle_is_null(pdone)) )
        copy_to_guest(pdone, &i, 1);
    return 0;
}
```

**支持的 Hypercall 命令**:
- `MMUEXT_MARK_XOM` (21): 标记页为 Execute-Only
- `MMUEXT_UNMARK_XOM` (22): 取消 Execute-Only 标记
- `MMUEXT_CREATE_XOM_SPAGES` (23): 创建子页
- `MMUEXT_WRITE_XOM_SPAGES` (24): 写入子页
- `MMUEXT_MARK_REG_CLEAR` (26): 标记寄存器清除页

## 三、内核模块实现

### 3.1 模块接口

**设备文件**: `/proc/xom`

**主要操作**:
- **mmap**: 分配 XOM 内存区域
- **write**: 发送命令（LOCK, FREE, INIT_SUBPAGES, WRITE_SUBPAGES, MARK_REG_CLEAR）
- **read**: 读取映射信息

### 3.2 内存映射

**mmap 处理**:

```572:611:modxom/modxom.c
static int xom_mmap(struct file *f, struct vm_area_struct *vma) {
    int status = -EBADF;
    pxom_process_entry curr_entry;
    pxom_mapping new_mapping;

#ifdef MODXOM_DEBUG
    printk(KERN_INFO "[MODXOM] xom_mmap(0x%lx -> 0x%lx, %lu pages), PID: %d\n",
        vma->vm_start, vma->vm_end, (vma->vm_end - vma->vm_start) / PAGE_SIZE, current->pid);
#endif

    if (!xen_hvm_domain())
        return -ENODEV;

    mutex_lock(&file_lock);

    curr_entry = get_process_entry();

    if (!curr_entry)
        goto exit;

    status = manage_mapping_intersection(vma, curr_entry);
    if (status < 0)
        goto exit;

    new_mapping = get_new_mapping(vma, curr_entry);

    if (!new_mapping)
        status = -EINVAL;
    else
        list_add(&(new_mapping->lhead), &(curr_entry->mappings));

exit:
    mutex_unlock(&file_lock);

#ifdef MODXOM_DEBUG
    printk(KERN_INFO "[MODXOM] xom_mmap returns %d\n", status);
#endif

    return status;
}
```

**关键步骤**:
1. **检查 HVM 域**: 确保在 Xen HVM Guest 中运行
2. **获取进程条目**: 每个进程维护自己的映射列表
3. **分配物理页**: `__get_free_pages()` - 分配连续物理页
4. **设置保留标志**: `SetPageReserved()` - 防止交换
5. **映射到用户空间**: `remap_pfn_range()` - 将物理页映射到用户虚拟地址

### 3.3 锁定页

**锁定操作**:

```247:284:modxom/modxom.c
static int lock_pages(pmodxom_cmd cmd) {
    unsigned page_index;
    pxom_process_entry curr_entry;
    pxom_mapping curr_mapping;

#ifdef MODXOM_DEBUG
    printk(KERN_INFO "[MODXOM] lock_pages(base_addr: 0x%lx, num_pages: 0x%u), PID: %d\n",
        cmd->base_addr, cmd->num_pages, current->pid);
#endif

    curr_entry = get_process_entry();
    if (!curr_entry)
        return -EBADF;

    curr_mapping = (pxom_mapping) curr_entry->mappings.next;
    while ((void *) curr_mapping != &(curr_entry->mappings)) {
        if (cmd->base_addr < curr_mapping->uaddr ||
            cmd->base_addr >= curr_mapping->uaddr + curr_mapping->num_pages * PAGE_SIZE) {
            curr_mapping = (pxom_mapping) curr_mapping->lhead.next;
            continue;
        }

        if (curr_mapping->subpage_level)
            goto fail;

        if (cmd->base_addr + cmd->num_pages * PAGE_SIZE > curr_mapping->uaddr + curr_mapping->num_pages * PAGE_SIZE)
            goto fail;

        page_index = (cmd->base_addr - curr_mapping->uaddr) / PAGE_SIZE;

        return xom_invoke_xen(curr_mapping, page_index, cmd->num_pages, MMUEXT_MARK_XOM);
    }
    fail:
#ifdef MODXOM_DEBUG
    printk(KERN_INFO "[MODXOM] lock_pages - Failed!, PID: %d\n", current->pid);
#endif
    return -EINVAL;
}
```

**调用 Hypervisor**:

```92:150:modxom/modxom.c
static int
xom_invoke_xen(pxom_mapping mapping, unsigned int page_index, unsigned int num_pages, unsigned int mmuext_cmd) {
    int status;
    struct mmuext_op op;
    pfn_t *gfns;
    unsigned int page_c, i, pages_locked = 0;
    unsigned long cur_gfn = mapping->pfn.val, base_gfn = cur_gfn, last_gfn;

    if (!num_pages)
        return 0;
    if (page_index + num_pages > mapping->num_pages)
        return -EINVAL;
    memset(&op, 0, sizeof(op));

    gfns = kvmalloc(sizeof(*gfns) * mapping->num_pages, GFP_KERNEL);
    if (!gfns)
        return -ENOMEM;

    while (pages_locked < num_pages) {
        page_c = 0;

        // Group into physically contiguous ranges
        do {
            page_c++;
            if (page_c + pages_locked >= mapping->num_pages)
                break;
            last_gfn = cur_gfn;
            cur_gfn = virt_to_phys((void *) (mapping->kaddr + (pages_locked + page_c) * PAGE_SIZE)) >> PAGE_SHIFT;
        } while (last_gfn == cur_gfn - 1);

        // Perform Hypercall for range
        op.cmd = mmuext_cmd;
        op.arg1.mfn = base_gfn;
        op.arg2.nr_ents = page_c;
#ifdef MODXOM_DEBUG
        printk(KERN_INFO "[MODXOM] Invoking Hypervisor with mfn 0x%lx for %u pages\n", op.arg1.mfn, op.arg2.nr_ents);
#endif
        status = hypercall(&op, 1, NULL, DOMID_SELF);
        if (status) {
#ifdef MODXOM_DEBUG
            printk(KERN_INFO "[MODXOM] Failed - Status 0x%x\n", status);
#endif
            status = -EINVAL;
            goto exit;
        }

        // Update lock status in mapping struct
        for (i = 0; i < page_c; i++)
            set_lock_status(mapping, page_index + pages_locked + i, 1);

        base_gfn = cur_gfn;
        pages_locked += page_c;
        // Repeat until all physically contiguous ranges are locked
    }

    exit:
    kvfree(gfns);
    return status;
}
```

**关键特性**:
- **物理连续分组**: 将虚拟连续的页分组为物理连续的页范围
- **批量 Hypercall**: 对每个物理连续范围执行一次 Hypercall
- **状态跟踪**: 使用位图跟踪哪些页已锁定

## 四、用户空间库实现

### 4.1 基本 API

**分配 XOM 缓冲区**:

```789:791:libxom/libxom.c
struct xombuf *xom_alloc(size_t size) {
    wrap_call(struct xombuf*, xomalloc_page_internal(size));
}
```

**写入数据**:

```799:801:libxom/libxom.c
int xom_write(struct xombuf *dest, const void *const restrict src, const size_t size, const size_t offset) {
    wrap_call(int, xom_write_internal(dest, src, size, offset));
}
```

**锁定缓冲区**:

```803:805:libxom/libxom.c
void *xom_lock(struct xombuf *buf) {
    wrap_call(void*, xom_lock_internal(buf));
}
```

### 4.2 锁定流程

**内部实现**:

```501:539:libxom/libxom.c
static void *xom_lock_internal(struct xombuf *buf) {
    int status, c = 0;
    modxom_cmd cmd;
    ssize_t size_left;

    if (!buf) {
        errno = EINVAL;
        return NULL;
    }

    if (buf->locked)
        return buf->address;

    if (buf->xom_mode == XOM_MODE_PKU) {
        if (mprotect(buf->address, SIZE_CEIL(buf->allocated_size), PROT_EXEC) < 0)
            return NULL;
        buf->locked = 1;
        return buf->address;
    }

    if (buf->pid != libxom_pid)
        return NULL;

    size_left = (ssize_t) buf->allocated_size;
    while (size_left > 0) {
        cmd = (modxom_cmd) {
                .cmd = MODXOM_CMD_LOCK,
                .num_pages = (uint32_t) SIZE_CEIL(min(size_left, ALLOC_CHUNK_SIZE)) >> PAGE_SHIFT,
                .base_addr = (uint64_t) (uintptr_t) buf->address + c * ALLOC_CHUNK_SIZE
        };
        status = (int) write(xomfd, &cmd, sizeof(cmd));
        if (status < 0)
            return NULL;
        size_left -= ALLOC_CHUNK_SIZE;
        c++;
    }
    buf->locked = 1;
    return buf->address;
}
```

**工作流程**:
1. **检查状态**: 如果已锁定，直接返回地址
2. **PKU 模式**: 使用 `mprotect()` 设置 PROT_EXEC
3. **EPT 模式**: 通过 `/proc/xom` 发送锁定命令
4. **批量处理**: 分块处理大缓冲区

### 4.3 子页 API

**分配子页缓冲区**:

```851:853:libxom/libxom.c
struct xom_subpages *xom_alloc_subpages(size_t size) {
    wrap_call(p_xom_subpages, xom_alloc_subpages_internal(size))
}
```

**填充并锁定子页**:

```855:857:libxom/libxom.c
void *xom_fill_and_lock_subpages(struct xom_subpages *dest, size_t size, const void *const src) {
    wrap_call(void*, xom_fill_and_lock_subpages_internal(dest, size, src))
}
```

**子页写入实现**:

```625:685:libxom/libxom.c
static void *write_into_subpages(struct xom_subpages *dest, size_t subpages_required, const void *restrict src,
                                 unsigned int base_page, unsigned int base_subpage, uint32_t mask) {
    int status;
    unsigned int i;
    unsigned int pkru;
    xom_subpage_write *write_cmd;

    if (dest->xom_mode == XOM_MODE_SLAT) {
        write_cmd = malloc(sizeof(*write_cmd));

        write_cmd->mxom_cmd = (modxom_cmd) {
                .cmd = MODXOM_CMD_WRITE_SUBPAGES,
                .num_pages = 1,
                .base_addr = (uint64_t) (uintptr_t) (dest->address + base_page * PAGE_SIZE),
        };

        write_cmd->xen_cmd.num_subpages = subpages_required;

        for (i = 0; i < subpages_required; i++) {
            write_cmd->xen_cmd.write_info[i].target_subpage = i + base_subpage;
            memcpy(write_cmd->xen_cmd.write_info[i].data, (char *) src + (i * SUBPAGE_SIZE), SUBPAGE_SIZE);
        }

        status = write(xomfd, write_cmd,
                       sizeof(write_cmd->mxom_cmd) + sizeof(write_cmd->xen_cmd.num_subpages) +
                       subpages_required * sizeof(*write_cmd->xen_cmd.write_info));

        free(write_cmd);

        if (status < 0)
            return NULL;
    } else if (dest->xom_mode == XOM_MODE_PKU) {
        // Transform XOM into WO for filling the subpage, then turn back into XOM
        asm volatile (
                "rdpkru"
                : "=a" (pkru)
                : "c" (0), "d" (0)
                );
        asm volatile(
                "wrpkru"
                ::"a" (pkru & ~(0x3 << (subpage_pkey << 1))), "c" (0), "d"(0)
                );

        memcpy(
                (char *) dest->address + base_page * PAGE_SIZE + base_subpage * SUBPAGE_SIZE,
                src,
                subpages_required * SUBPAGE_SIZE
        );

        asm volatile (
                "wrpkru\n"
                ::"a" (pkru), "c" (0), "d" (0)
                );
    } else
        return NULL;


    dest->lock_status[base_page] |= mask << base_subpage;
    dest->references++;
    return dest->address + base_page * PAGE_SIZE + base_subpage * SUBPAGE_SIZE;
}
```

## 五、使用示例

### 5.1 基本使用流程

```c
#include "xom.h"

int main() {
    struct xombuf *xbuf;
    unsigned int (*secret_func)(unsigned int);

    // 1. 分配 XOM 缓冲区
    xbuf = xom_alloc(PAGE_SIZE);
    if (!xbuf)
        return errno;

    // 2. 写入敏感代码
    if (xom_write(xbuf, secret_function, secret_function_size, 0) <= 0)
        return errno;

    // 3. 锁定缓冲区（设置为 Execute-Only）
    secret_func = xom_lock(xbuf);
    if (!secret_func)
        return errno;

    // 4. 清除原始代码
    memset(secret_function, 0, secret_function_size);

    // 5. 使用 XOM 保护的函数
    unsigned int result = secret_func(0xdeadbeef);

    // 6. 释放缓冲区
    xom_free(xbuf);

    return 0;
}
```

### 5.2 子页使用

```c
struct xom_subpages *subpages = xom_alloc_subpages(PAGE_SIZE);

// 逐步写入小数据块
void *ptr1 = xom_fill_and_lock_subpages(subpages, 64, data1);
void *ptr2 = xom_fill_and_lock_subpages(subpages, 128, data2);

// 使用后释放
xom_free_subpages(subpages, ptr1);
xom_free_subpages(subpages, ptr2);
```

### 5.3 寄存器清除标记

```c
// 标记页为寄存器清除页（完全清除）
xom_mark_register_clear(xbuf, 1, 0);  // full_clear=1, page_number=0

// 在代码中使用
expect_full_register_clear {
    // 这段代码执行时，如果发生中断，所有寄存器（除 RBP/RSP）将被清除
    sensitive_operation();
}
```

## 六、安全机制

### 6.1 EPT 硬件保护

**Execute-Only 权限**:
- ✅ **只能执行**: 代码可以执行，但不能读取或写入
- ✅ **硬件强制**: 由 Intel EPT 硬件强制执行
- ✅ **无法绕过**: Guest 内核无法绕过 Hypervisor 设置的权限

**EPT 页表项**:
```
EPT Entry:
  - R (Read) = 0
  - W (Write) = 0
  - X (Execute) = 1
```

### 6.2 寄存器清除

**目的**: 防止侧信道攻击通过寄存器泄露敏感信息

**清除时机**:
- 中断处理
- 异常处理
- VM Exit

**清除类型**:
- **VECTOR**: 清除 R15 和 SSE/AVX 寄存器
- **FULL**: 清除所有寄存器（除 RBP/RSP）

### 6.3 子页锁定

**一次性写入**: 每个子页只能写入一次，防止修改已锁定的代码

**锁定状态跟踪**: 使用位图跟踪子页锁定状态

## 七、限制和注意事项

### 7.1 硬件要求

- ✅ **Intel CPU**: 需要支持 EPT（Extended Page Tables）
- ❌ **AMD CPU**: 不支持（AMD 使用 NPT，不支持 Execute-Only）
- ✅ **Xen HVM Guest**: 必须在硬件加速的虚拟机中运行

### 7.2 性能影响

- **TLB 刷新**: 每次权限更改需要刷新 TLB
- **Hypercall 开销**: 每次操作需要 Hypercall
- **寄存器清除**: 中断处理时清除寄存器有性能开销

### 7.3 安全考虑

- ⚠️ **研究工具**: Lixom 是研究工具，不建议用于生产环境
- ⚠️ **侧信道攻击**: 虽然提供寄存器清除，但可能仍有其他侧信道攻击向量
- ⚠️ **代码完整性**: 需要确保代码在锁定前已完全写入

## 八、批量页面处理机制

### 8.1 问题：如何高效处理大量页面？

当需要保护大量页面时，如果每个页面都发送一次 Hypercall，会产生大量开销。Lixom 通过以下机制优化批量处理：

1. **物理连续页面分组**
2. **大块内存分配**
3. **批量 Hypercall**

### 8.2 物理连续页面分组

**核心机制**: 内核模块会将物理连续的页面分组，对每个物理连续范围执行一次 Hypercall。

**实现** (`xom_invoke_xen()`):

```92:150:modxom/modxom.c
static int
xom_invoke_xen(pxom_mapping mapping, unsigned int page_index, unsigned int num_pages, unsigned int mmuext_cmd) {
    int status;
    struct mmuext_op op;
    pfn_t *gfns;
    unsigned int page_c, i, pages_locked = 0;
    unsigned long cur_gfn = mapping->pfn.val, base_gfn = cur_gfn, last_gfn;

    if (!num_pages)
        return 0;
    if (page_index + num_pages > mapping->num_pages)
        return -EINVAL;
    memset(&op, 0, sizeof(op));

    gfns = kvmalloc(sizeof(*gfns) * mapping->num_pages, GFP_KERNEL);
    if (!gfns)
        return -ENOMEM;

    while (pages_locked < num_pages) {
        page_c = 0;

        // Group into physically contiguous ranges
        do {
            page_c++;
            if (page_c + pages_locked >= mapping->num_pages)
                break;
            last_gfn = cur_gfn;
            cur_gfn = virt_to_phys((void *) (mapping->kaddr + (pages_locked + page_c) * PAGE_SIZE)) >> PAGE_SHIFT;
        } while (last_gfn == cur_gfn - 1);

        // Perform Hypercall for range
        op.cmd = mmuext_cmd;
        op.arg1.mfn = base_gfn;
        op.arg2.nr_ents = page_c;
#ifdef MODXOM_DEBUG
        printk(KERN_INFO "[MODXOM] Invoking Hypervisor with mfn 0x%lx for %u pages\n", op.arg1.mfn, op.arg2.nr_ents);
#endif
        status = hypercall(&op, 1, NULL, DOMID_SELF);
        if (status) {
#ifdef MODXOM_DEBUG
            printk(KERN_INFO "[MODXOM] Failed - Status 0x%x\n", status);
#endif
            status = -EINVAL;
            goto exit;
        }

        // Update lock status in mapping struct
        for (i = 0; i < page_c; i++)
            set_lock_status(mapping, page_index + pages_locked + i, 1);

        base_gfn = cur_gfn;
        pages_locked += page_c;
        // Repeat until all physically contiguous ranges are locked
    }

    exit:
    kvfree(gfns);
    return status;
}
```

**分组算法**:
1. **起始页**: 从第一个页面开始
2. **检查连续性**: 检查下一个页面的物理地址是否连续（`cur_gfn == last_gfn + 1`）
3. **累积连续范围**: 如果连续，增加 `page_c` 计数
4. **执行 Hypercall**: 对每个物理连续范围执行一次 Hypercall
5. **重复**: 直到所有页面处理完毕

**示例**:
```
虚拟地址: [0x1000, 0x2000, 0x3000, 0x4000, 0x5000]
物理地址: [0x10000, 0x10100, 0x10200, 0x10300, 0x20000]
          └───────── 连续 ─────────┘  └─ 不连续 ─┘

分组结果:
  - 范围1: 物理地址 0x10000-0x10300 (4页) → 1次 Hypercall
  - 范围2: 物理地址 0x20000 (1页) → 1次 Hypercall

总共: 2次 Hypercall（而不是5次）
```

### 8.3 大块内存分配

**目的**: 通过分配大块连续物理内存，提高物理连续性，减少 Hypercall 次数。

**分配策略** (`get_new_mapping()`):

```321:376:modxom/modxom.c
static pxom_mapping get_new_mapping(struct vm_area_struct *vma, pxom_process_entry curr_entry) {
    unsigned long size = (vma->vm_end - vma->vm_start);
    void *newmem = NULL;
    uint8_t *n_lock_status = NULL;
    unsigned int i;
    int status;
    pfn_t pfn;
    pxom_mapping new_mapping = NULL;

    if (!curr_entry)
        return NULL;

    // Must be page-aligned
    if (size % PAGE_SIZE || vma->vm_start % PAGE_SIZE || !size) {
        return NULL;
    }

    if (size > (1 << (MAX_ORDER - 1)) << PAGE_SHIFT)
        return NULL;

    new_mapping = kmalloc(sizeof(*new_mapping), GFP_KERNEL);
    if (!new_mapping)
        return NULL;

    n_lock_status = kmalloc(((size / PAGE_SIZE) >> 3) + 1, GFP_KERNEL);
    if (!n_lock_status)
        goto fail;

    memset(n_lock_status, 0, ((size / PAGE_SIZE) >> 3) + 1);

    newmem = (void *) __get_free_pages(GFP_KERNEL, get_order(size));
    if (!newmem || (ssize_t) newmem == -1)
        goto fail;

    // Set PG_reserved bit to prevent swapping
    for (i = 0; i < size; i += PAGE_SIZE)
        SetPageReserved(virt_to_page(newmem + i));

    memset(newmem, 0x0, PAGE_SIZE * (1 << get_order(size)));

    pfn = (pfn_t) {virt_to_phys(newmem) >> PAGE_SHIFT};
    status = remap_pfn_range(vma, vma->vm_start, pfn.val, size, PAGE_SHARED_EXEC);

    if (status < 0)
        goto fail;

    *new_mapping = (xom_mapping) {
            .num_pages = size / PAGE_SIZE,
            .uaddr = vma->vm_start,
            .kaddr = (unsigned long) newmem,
            .subpage_level = false,
            .pfn = pfn,
            .lock_status = n_lock_status
    };

    return new_mapping;
```

**关键点**:
- **`__get_free_pages()`**: 分配连续物理页
- **`get_order(size)`**: 计算所需页数（2的幂次）
- **最大大小**: `(1 << (MAX_ORDER - 1)) << PAGE_SHIFT = 4MB`（MAX_ORDER=11时）

**优势**:
- ✅ **物理连续**: `__get_free_pages()` 保证分配的页面在物理上连续
- ✅ **减少 Hypercall**: 连续页面可以一次处理
- ✅ **性能优化**: 减少系统调用和上下文切换

### 8.4 用户空间批量处理

**分块策略** (`xom_lock_internal()`):

```501:539:libxom/libxom.c
static void *xom_lock_internal(struct xombuf *buf) {
    int status, c = 0;
    modxom_cmd cmd;
    ssize_t size_left;

    if (!buf) {
        errno = EINVAL;
        return NULL;
    }

    if (buf->locked)
        return buf->address;

    if (buf->xom_mode == XOM_MODE_PKU) {
        if (mprotect(buf->address, SIZE_CEIL(buf->allocated_size), PROT_EXEC) < 0)
            return NULL;
        buf->locked = 1;
        return buf->address;
    }

    if (buf->pid != libxom_pid)
        return NULL;

    size_left = (ssize_t) buf->allocated_size;
    while (size_left > 0) {
        cmd = (modxom_cmd) {
                .cmd = MODXOM_CMD_LOCK,
                .num_pages = (uint32_t) SIZE_CEIL(min(size_left, ALLOC_CHUNK_SIZE)) >> PAGE_SHIFT,
                .base_addr = (uint64_t) (uintptr_t) buf->address + c * ALLOC_CHUNK_SIZE
        };
        status = (int) write(xomfd, &cmd, sizeof(cmd));
        if (status < 0)
            return NULL;
        size_left -= ALLOC_CHUNK_SIZE;
        c++;
    }
    buf->locked = 1;
    return buf->address;
}
```

**分块大小**: `ALLOC_CHUNK_SIZE = (1 << (MAX_ORDER - 1)) << PAGE_SHIFT = 4MB`

**处理流程**:
1. **分块**: 将大缓冲区分成 4MB 的块
2. **逐块处理**: 对每个块发送一次命令
3. **内核优化**: 内核模块进一步将物理连续页面分组

### 8.5 Hypervisor 层批量处理

**Hypervisor 实现** (`set_xom_seal()`):

```116:146:xen/xen/arch/x86/xom_seal.c
static int set_xom_seal(struct domain* d, gfn_t gfn, unsigned int nr_pages){
    int ret = 0;
    unsigned int i;
    struct p2m_domain *p2m;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING, "Entered set_xom_seal with gfn 0x%lx for %u pages. Max mapped page is 0x%lx\n", gfn_x(gfn) , nr_pages, p2m->max_mapped_pfn);

    if (!nr_pages)
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > p2m->max_mapped_pfn )
        return -EOVERFLOW;

    for ( i = 0; i < nr_pages; i++) {
        c_gfn = _gfn(gfn_x(gfn) + i);
        gfn_lock(p2m, c_gfn, 0);
        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_x, c_gfn);
        gfn_unlock(p2m, c_gfn, 0);
        if (ret < 0)
            break;
    }

    p2m->tlb_flush(p2m);
    return ret;
}
```

**特点**:
- ✅ **接受多页参数**: `nr_pages` 参数允许一次处理多个页面
- ✅ **循环处理**: 虽然循环处理每个页面，但只需要一次 Hypercall
- ✅ **批量 TLB 刷新**: 所有页面处理完后才刷新 TLB，而不是每页刷新

**优化点**:
- **TLB 刷新优化**: 所有页面处理完后统一刷新 TLB，而不是每页刷新
- **错误处理**: 如果某个页面失败，立即返回，不继续处理

### 8.6 完整处理流程示例

**场景**: 需要保护 16MB 的代码（4096 页）

**处理流程**:

```
1. 用户空间 (libxom):
   - 分配 16MB 缓冲区
   - 写入代码
   - 调用 xom_lock()

2. 用户空间分块 (libxom):
   - 块1: 0x0 - 0x400000 (4MB, 1024页) → 发送命令1
   - 块2: 0x400000 - 0x800000 (4MB, 1024页) → 发送命令2
   - 块3: 0x800000 - 0xC00000 (4MB, 1024页) → 发送命令3
   - 块4: 0xC00000 - 0x1000000 (4MB, 1024页) → 发送命令4

3. 内核模块 (modxom):
   对于每个命令，检查物理连续性：

   命令1 (1024页):
     - 如果物理连续 → 1次 Hypercall (1024页)
     - 如果不连续 → N次 Hypercall (N个连续范围)

   命令2-4: 同样处理

4. Hypervisor (xom_seal.c):
   对于每次 Hypercall:
     - 循环处理 nr_pages 个页面
     - 统一刷新 TLB
     - 返回结果

最终结果:
  - 如果所有页面物理连续: 4次 Hypercall (每块1次)
  - 如果部分不连续: 4+N次 Hypercall (取决于物理连续性)
```

### 8.7 性能优化策略

#### 8.7.1 物理连续性保证

**方法**: 使用 `__get_free_pages()` 分配连续物理页

**优势**:
- ✅ **保证连续**: 分配的页面在物理上连续
- ✅ **减少 Hypercall**: 连续页面可以一次处理
- ✅ **提高效率**: 减少系统调用开销

#### 8.7.2 批量 TLB 刷新

**方法**: 所有页面处理完后统一刷新 TLB

**优势**:
- ✅ **减少开销**: 避免每页刷新 TLB
- ✅ **提高性能**: 批量操作更高效

#### 8.7.3 分组处理

**方法**: 将物理连续的页面分组，每组一次 Hypercall

**优势**:
- ✅ **自适应**: 自动适应物理内存布局
- ✅ **最优分组**: 最大化每次 Hypercall 处理的页面数

### 8.8 限制和注意事项

#### 8.8.1 物理连续性限制

**问题**: 如果页面在物理上不连续，需要多次 Hypercall

**解决方案**:
- ✅ **大块分配**: 使用 `__get_free_pages()` 保证连续
- ✅ **分组优化**: 自动将连续页面分组

#### 8.8.2 最大块大小限制

**限制**: `MAX_ORDER = 11`，最大块大小为 4MB

**影响**:
- 超过 4MB 的缓冲区需要分块处理
- 每个块独立处理，但内核会进一步优化

#### 8.8.3 Hypercall 参数限制

**检查**: Hypervisor 层没有明确的页面数限制，但受以下因素影响：
- **GFN 范围**: `gfn_x(gfn) + nr_pages <= p2m->max_mapped_pfn`
- **内存限制**: 受域的最大映射页数限制

### 8.9 最佳实践

#### 8.9.1 分配策略

**推荐**: 使用大块连续分配

```c
// 推荐：一次性分配大块
struct xombuf *buf = xom_alloc(16 * 1024 * 1024);  // 16MB

// 不推荐：多次分配小块
for (int i = 0; i < 16; i++) {
    struct xombuf *small = xom_alloc(1024 * 1024);  // 每次1MB
}
```

#### 8.9.2 锁定时机

**推荐**: 写入完成后立即锁定

```c
// 推荐：写入后立即锁定
xom_write(buf, data, size, 0);
void *locked = xom_lock(buf);  // 立即锁定

// 不推荐：延迟锁定
xom_write(buf, data, size, 0);
// ... 其他操作 ...
void *locked = xom_lock(buf);  // 延迟锁定，增加风险
```

#### 8.9.3 批量操作

**推荐**: 一次性锁定所有需要的页面

```c
// 推荐：一次性锁定
struct xombuf *buf = xom_alloc(total_size);
xom_write(buf, all_data, total_size, 0);
xom_lock(buf);  // 一次锁定所有页面

// 不推荐：分多次锁定
for (int i = 0; i < chunks; i++) {
    xom_write(buf, chunk[i], chunk_size, offset);
    xom_lock_partial(buf, offset, chunk_size);  // 多次锁定
}
```

## 九、总结

### 9.1 核心机制

1. **EPT Execute-Only**: 利用 Intel EPT 硬件特性实现硬件级别的执行保护
2. **寄存器清除**: 在中断/异常时清除寄存器，防止信息泄露
3. **子页机制**: 支持细粒度（128 字节）的保护和逐步写入
4. **Hypervisor 控制**: 所有权限设置由 Hypervisor 控制，Guest 无法绕过

### 8.2 应用场景

- **加密密钥保护**: 保护 AES、HMAC 等加密算法的密钥
- **敏感代码保护**: 保护包含敏感逻辑的代码段
- **侧信道防护**: 通过寄存器清除减少侧信道攻击风险

### 8.3 优势

- ✅ **硬件强制**: 由 CPU 硬件强制执行，无法绕过
- ✅ **细粒度控制**: 支持页级和子页级保护
- ✅ **透明使用**: 用户空间 API 简单易用

### 8.4 参考

- [Lixom 论文](https://www.usenix.org/conference/usenixsecurity23/presentation/hornetz) - "Lixom: Protecting Encryption Keys with Execute-Only Memory"
- [Xen EPT 文档](../xen-hypervisor/iommu-support.md) - EPT 相关说明
- [Xen Hypercall 文档](../xen-hypervisor/xen-hypercalls.md) - Hypercall 接口说明
