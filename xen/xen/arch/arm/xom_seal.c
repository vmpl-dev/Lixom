/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM XOM (Execute-Only Memory) seal implementation.
 * Stage-2 equivalent of x86 EPT-XOM for Lixom.
 */

#include <xen/domain_page.h>
#include <xen/guest_access.h>
#include <xen/mem_access.h>
#include <xen/rbtree.h>
#include <xen/sched.h>
#include <xen/xmalloc.h>
#include <xen/xom_seal.h>
#include <public/xen.h>

#include <asm/guest_walk.h>
#include <asm/mm.h>
#include <asm/page.h>
#include <asm/p2m.h>

#define SUBPAGE_SIZE (PAGE_SIZE / (sizeof(uint32_t) << 3))
#define MAX_SUBPAGES_PER_CMD ((PAGE_SIZE - sizeof(uint8_t)) / (sizeof(xom_subpage_write_info)))

#ifndef XEN_XOM_SEAL_DEBUG
#define gdprintk(...)
#endif

typedef union {
    uint32_t lock_status;
    uint8_t reg_clear_type;
} info_spec_t;

struct xom_page_info {
    struct rb_node node;
    gfn_t gfn;
    info_spec_t info;
};

struct xom_subpage_write_info {
    uint8_t target_subpage;
    uint8_t data[SUBPAGE_SIZE];
};

struct xom_subpage_write_command {
    uint8_t num_subpages;
    struct xom_subpage_write_info write_info[MAX_SUBPAGES_PER_CMD];
};

static int insert_page_info_node(struct rb_root *root, struct xom_page_info *info)
{
    struct rb_node **new, *parent;
    struct xom_page_info *this;

    new = &root->rb_node;
    parent = NULL;

    while ( *new )
    {
        this = container_of(*new, struct xom_page_info, node);
        parent = *new;
        if ( gfn_x(info->gfn) < gfn_x(this->gfn) )
            new = &((*new)->rb_left);
        else if ( gfn_x(info->gfn) > gfn_x(this->gfn) )
            new = &((*new)->rb_right);
        else
            return -EEXIST;
    }

    rb_link_node(&info->node, parent, new);
    rb_insert_color(&info->node, root);
    return 0;
}

static struct xom_page_info *add_page_info_entry(struct rb_root *root,
                                                const gfn_t gfn,
                                                info_spec_t info)
{
    struct xom_page_info *data = xmalloc(struct xom_page_info);

    if ( !data )
        return NULL;

    *data = (struct xom_page_info){
        .gfn = gfn,
        .info = info,
    };
    RB_CLEAR_NODE(&data->node);

    if ( insert_page_info_node(root, data) < 0 )
    {
        xfree(data);
        return NULL;
    }

    return data;
}

static void rm_page_info_entry(struct rb_root *root, struct xom_page_info *data)
{
    rb_erase(&data->node, root);
    xfree(data);
}

static struct xom_page_info *get_page_info_entry(struct rb_root *root,
                                                 const gfn_t gfn)
{
    struct rb_node *node;
    struct xom_page_info *entry;

    if ( !root )
        return NULL;

    node = root->rb_node;

    while ( node )
    {
        entry = container_of(node, struct xom_page_info, node);
        if ( gfn_x(gfn) < gfn_x(entry->gfn) )
            node = node->rb_left;
        else if ( gfn_x(gfn) > gfn_x(entry->gfn) )
            node = node->rb_right;
        else
            return entry;
    }

    return NULL;
}

static int set_xom_seal(struct domain *d, gfn_t gfn, unsigned int nr_pages)
{
    int ret = 0;
    unsigned int i;
    struct p2m_domain *p2m;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING,
             "Entered set_xom_seal with gfn 0x%lx for %u pages. Max mapped gfn 0x%lx\n",
             gfn_x(gfn), nr_pages, gfn_x(p2m->max_mapped_gfn));

    if ( !nr_pages )
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > gfn_x(p2m->max_mapped_gfn) + 1 )
        return -EOVERFLOW;

    p2m_write_lock(p2m);
    for ( i = 0; i < nr_pages; i++ )
    {
        c_gfn = gfn_add(gfn, i);
        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_x, c_gfn);
        if ( ret < 0 )
            break;
    }
    p2m_tlb_flush_sync(p2m);
    p2m_write_unlock(p2m);

    return ret;
}

static int clear_xom_seal(struct domain *d, gfn_t gfn, unsigned int nr_pages)
{
    int ret = 0;
    unsigned int i;
    void *xom_page;
    struct p2m_domain *p2m;
    struct page_info *page;
    struct xom_page_info *page_info;
    p2m_type_t ptype;
    p2m_access_t atype;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING,
             "Entered clear_xom_seal with gfn 0x%lx for %u pages. Max mapped gfn 0x%lx\n",
             gfn_x(gfn), nr_pages, gfn_x(p2m->max_mapped_gfn));

    if ( !nr_pages )
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > gfn_x(p2m->max_mapped_gfn) + 1 )
        return -EOVERFLOW;

    p2m_write_lock(p2m);
    for ( i = 0; i < nr_pages; i++ )
    {
        c_gfn = gfn_add(gfn, i);

        p2m_get_entry(p2m, c_gfn, &ptype, &atype, NULL, NULL);
        if ( atype != p2m_access_x )
            continue;

        page = get_page_from_gfn(d, gfn_x(c_gfn), NULL, P2M_ALLOC);
        if ( !page )
        {
            ret = -EINVAL;
            goto exit;
        }

        if ( !get_page_type(page, PGT_writable_page) )
        {
            put_page(page);
            ret = -EPERM;
            goto exit;
        }

        xom_page = map_domain_page(page);
        memset(xom_page, 0x90, PAGE_SIZE);
        unmap_domain_page(xom_page);
        put_page_and_type(page);

        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_rwx, c_gfn);
        if ( ret < 0 )
            goto exit;

        page_info = get_page_info_entry(&d->xom_subpages, c_gfn);
        if ( page_info )
            rm_page_info_entry(&d->xom_subpages, page_info);
        page_info = get_page_info_entry(&d->xom_reg_clear_pages, c_gfn);
        if ( page_info )
            rm_page_info_entry(&d->xom_reg_clear_pages, page_info);
    }

 exit:
    p2m_tlb_flush_sync(p2m);
    p2m_write_unlock(p2m);
    return ret;
}

static int create_xom_subpages(struct domain *d, gfn_t gfn, unsigned int nr_pages)
{
    int ret = 0;
    unsigned int i;
    struct p2m_domain *p2m;
    struct xom_page_info *subpage_info = NULL;
    p2m_type_t ptype;
    p2m_access_t atype;
    gfn_t c_gfn;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    gdprintk(XENLOG_WARNING,
             "Entered create_subpages with gfn 0x%lx for %u pages. Max mapped gfn 0x%lx\n",
             gfn_x(gfn), nr_pages, gfn_x(p2m->max_mapped_gfn));

    if ( !nr_pages )
        return -EINVAL;

    if ( gfn_x(gfn) + nr_pages > gfn_x(p2m->max_mapped_gfn) + 1 )
        return -EOVERFLOW;

    p2m_write_lock(p2m);
    for ( i = 0; i < nr_pages; i++ )
    {
        c_gfn = gfn_add(gfn, i);

        p2m_get_entry(p2m, c_gfn, &ptype, &atype, NULL, NULL);
        if ( atype == p2m_access_x )
        {
            ret = -EINVAL;
            goto exit;
        }

        ret = p2m_set_mem_access_single(d, p2m, NULL, p2m_access_x, c_gfn);
        if ( ret < 0 )
            goto exit;

        subpage_info = add_page_info_entry(&d->xom_subpages, c_gfn,
                                           (info_spec_t){ .lock_status = 0 });
        if ( !subpage_info )
        {
            ret = -ENOMEM;
            goto exit;
        }
    }

 exit:
    p2m_tlb_flush_sync(p2m);
    p2m_write_unlock(p2m);
    return ret;
}

static int write_into_subpage(struct domain *d, gfn_t gfn_dest, gfn_t gfn_src)
{
    unsigned int i;
    char *xom_page, *write_dest;
    struct p2m_domain *p2m;
    struct page_info *page;
    struct xom_page_info *subpage_info;
    struct xom_subpage_write_command command;

    subpage_info = get_page_info_entry(&d->xom_subpages, gfn_dest);
    if ( !subpage_info )
        return -EINVAL;

    p2m = p2m_get_hostp2m(d);

    if ( unlikely(!p2m) )
        return -EFAULT;

    if ( gfn_x(gfn_src) > gfn_x(p2m->max_mapped_gfn) )
        return -EOVERFLOW;

    page = get_page_from_gfn(d, gfn_x(gfn_src), NULL, P2M_ALLOC);
    if ( !page )
        return -EINVAL;
    xom_page = (char *)map_domain_page(page);
    memcpy(&command, xom_page, sizeof(command));
    unmap_domain_page(xom_page);
    put_page(page);

    gdprintk(XENLOG_WARNING, "Copying %u subpages from %lx to %lx\n",
             command.num_subpages, gfn_x(gfn_src), gfn_x(gfn_dest));

    if ( command.num_subpages > MAX_SUBPAGES_PER_CMD )
        return -EINVAL;
    for ( i = 0; i < command.num_subpages; i++ )
    {
        if ( command.write_info[i].target_subpage >= (PAGE_SIZE / SUBPAGE_SIZE) )
            return -EINVAL;
        if ( subpage_info->info.lock_status & (1 << command.write_info[i].target_subpage) )
            return -EINVAL;
    }

    page = get_page_from_gfn(d, gfn_x(gfn_dest), NULL, P2M_ALLOC);
    if ( !page )
        return -EINVAL;
    if ( !get_page_type(page, PGT_writable_page) )
    {
        put_page(page);
        return -EPERM;
    }
    xom_page = (char *)map_domain_page(page);
    for ( i = 0; i < command.num_subpages; i++ )
    {
        write_dest = xom_page + (command.write_info[i].target_subpage * SUBPAGE_SIZE);
        memcpy(write_dest, command.write_info[i].data, SUBPAGE_SIZE);
        subpage_info->info.lock_status |= 1 << command.write_info[i].target_subpage;
    }
    unmap_domain_page(xom_page);
    put_page_and_type(page);

    return 0;
}

static int mark_reg_clear_page(struct domain *d, gfn_t gfn,
                               unsigned int reg_clear_type)
{
    struct xom_page_info *page_info;
    struct p2m_domain *p2m;
    p2m_type_t ptype;
    p2m_access_t atype;

    if ( !reg_clear_type || reg_clear_type > REG_CLEAR_TYPE_FULL )
        return -EINVAL;

    page_info = get_page_info_entry(&d->xom_reg_clear_pages, gfn);
    if ( page_info )
        return -EINVAL;

    p2m = p2m_get_hostp2m(d);
    p2m_read_lock(p2m);
    p2m_get_entry(p2m, gfn, &ptype, &atype, NULL, NULL);
    p2m_read_unlock(p2m);
    if ( atype != p2m_access_x )
        return -EINVAL;

    page_info = add_page_info_entry(&d->xom_reg_clear_pages, gfn,
                                    (info_spec_t){ .reg_clear_type = reg_clear_type });
    if ( !page_info )
        return -ENOMEM;

    return 0;
}

int handle_xom_seal(struct vcpu *curr,
                    XEN_GUEST_HANDLE_PARAM(mmuext_op_t) uops,
                    unsigned int count,
                    XEN_GUEST_HANDLE_PARAM(uint) pdone)
{
    int rc;
    unsigned int i;
    struct domain *d = curr->domain;
    struct mmuext_op op;

    for ( i = 0; i < count; i++ )
    {
        if ( (i && hypercall_preempt_check()) )
        {
            gdprintk(XENLOG_ERR, "Preempt check failed\n");
            return -ERESTART;
        }

        if ( unlikely(__copy_from_guest(&op, uops, 1) != 0) )
        {
            gdprintk(XENLOG_ERR, "Unable to copy guest op\n");
            return -EFAULT;
        }

        spin_lock(&d->xom_page_lock);
        switch ( op.cmd )
        {
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
            rc = mark_reg_clear_page(d, _gfn(op.arg1.mfn), op.arg2.nr_ents);
            break;
        default:
            rc = -EOPNOTSUPP;
        }
        spin_unlock(&d->xom_page_lock);

        guest_handle_add_offset(uops, 1);
        if ( rc < 0 )
            return rc;
    }

    if ( unlikely(!guest_handle_is_null(pdone)) )
        copy_to_guest(pdone, &i, 1);
    return 0;
}

static void free_xom_info_node(struct rb_node *node)
{
    struct xom_page_info *data;

    if ( !node )
        return;

    data = rb_entry(node, struct xom_page_info, node);

    if ( node->rb_left )
        free_xom_info_node(node->rb_left);
    if ( node->rb_right )
        free_xom_info_node(node->rb_right);

    xfree(data);
}

void free_xom_info(struct rb_root *root)
{
    if ( root->rb_node )
        free_xom_info_node(root->rb_node);

    *root = RB_ROOT;
}

static gfn_t gfn_of_pc(register_t pc)
{
    struct vcpu *v = current;
    paddr_t ipa;
    unsigned int perms;

    if ( unlikely(!v || !v->domain) )
        return INVALID_GFN;

    if ( unlikely(!v->is_initialised) )
        return INVALID_GFN;

    if ( !guest_walk_tables(v, (vaddr_t)pc, &ipa, &perms) )
        return INVALID_GFN;

    return gaddr_to_gfn(ipa);
}

unsigned char get_reg_clear_type(const struct cpu_user_regs *regs)
{
    unsigned char ret = REG_CLEAR_TYPE_NONE;
    struct xom_page_info *info;
    gfn_t instr_gfn;
    struct vcpu *v = current;
    struct domain *d;

    if ( !regs )
        return ret;
    if ( !v || !(d = v->domain) )
        return ret;

    instr_gfn = gfn_of_pc(regs->pc);
    if ( unlikely(gfn_eq(instr_gfn, INVALID_GFN)) )
        return ret;

    spin_lock(&d->xom_page_lock);

    info = get_page_info_entry(&d->xom_reg_clear_pages, instr_gfn);
    if ( info )
        ret = info->info.reg_clear_type;

    spin_unlock(&d->xom_page_lock);
    return ret;
}
