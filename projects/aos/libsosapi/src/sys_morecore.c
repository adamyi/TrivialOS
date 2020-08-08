/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>
#include "sos.h"

/* Actual morecore implementation
   returns 0 if failure, returns newbrk if success.
*/

long sys_brk(va_list ap)
{
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    return sos_sys_brk(newbrk);
}

long sys_mmap(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);

    return sos_sys_mmap((uintptr_t) addr, length, prot);
}

long sys_munmap(va_list ap)
{
    void *addr = va_arg(ap, void *);
    size_t length = va_arg(ap, size_t);
    return sos_sys_munmap((uintptr_t) addr, length);
}
