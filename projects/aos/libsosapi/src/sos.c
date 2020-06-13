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
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sos.h>

#include <sel4/sel4.h>

#define SYSCALL_ENDPOINT_SLOT          (1)
#define SHARED_BUFFER_VADDR            (0xA0001000)
#define PAGE_SIZE_4K                   (0x1000)

int sos_sys_open(const char *path, fmode_t mode)
{
    // assert(!"You need to implement this");
    printf("Calling open\n");
    seL4_SetMR(0, 0);
    seL4_SetMR(1, path);
    seL4_SetMR(2, mode);
    seL4_SetMR(3, 0);
    memcpy(SHARED_BUFFER_VADDR, path, PAGE_SIZE_4K);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_sys_close(int file)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_sys_read(int file, char *buf, size_t nbyte)
{
    // printf("Calling read\n");
    return 0;
}

int sos_sys_write(int file, const char *buf, size_t nbyte)
{
    printf("Fucking write\n");
    //return sos_write2(buf, nbyte);
    seL4_SetMR(0, 2);
    seL4_SetMR(1, file);
    seL4_SetMR(2, buf);
    if (nbyte > PAGE_SIZE_4K) nbyte = PAGE_SIZE_4K;
    seL4_SetMR(3, nbyte);
    memcpy(SHARED_BUFFER_VADDR, buf, PAGE_SIZE_4K);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_process_create(const char *path)
{
    assert(!"You need to implement this");
    return -1;
}

int sos_process_delete(pid_t pid)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_my_id(void)
{
    assert(!"You need to implement this");
    return -1;

}

int sos_process_status(sos_process_t *processes, unsigned max)
{
    assert(!"You need to implement this");
    return -1;
}

pid_t sos_process_wait(pid_t pid)
{
    assert(!"You need to implement this");
    return -1;

}

void sos_sys_usleep(int msec)
{
    assert(!"You need to implement this");
}

int64_t sos_sys_time_stamp(void)
{
    assert(!"You need to implement this");
    return -1;
}
