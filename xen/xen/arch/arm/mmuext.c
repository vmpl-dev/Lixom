/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM mmuext_op hypercall handler.
 * Only supports XOM-related operations (MMUEXT_MARK_XOM, etc.);
 * dispatches to handle_xom_seal. Other ops return -EOPNOTSUPP.
 */

#include <xen/errno.h>
#include <xen/guest_access.h>
#include <xen/xom_seal.h>

#include <asm/current.h>

#include <public/xen.h>

long do_mmuext_op(XEN_GUEST_HANDLE_PARAM(mmuext_op_t) uops,
                  unsigned int count,
                  XEN_GUEST_HANDLE_PARAM(uint) pdone,
                  unsigned int foreigndom)
{
    if ( unlikely(!guest_handle_okay(uops, count)) )
        return -EFAULT;

    /* On ARM we only support XOM seal operations; handle_xom_seal
     * returns -EOPNOTSUPP for any other mmuext cmd. */
    return handle_xom_seal(current, uops, count, pdone);
}
