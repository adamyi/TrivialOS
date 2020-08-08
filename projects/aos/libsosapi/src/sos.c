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
#define SHARED_BUFFER_VADDR            (0xA000001000)
#define PAGE_SIZE_4K                   (0x1000)

#define SYSCALL_NO_OPEN           (0)
#define SYSCALL_NO_READ           (1)
#define SYSCALL_NO_WRITE          (2)
#define SYSCALL_NO_CLOSE          (3)
#define SYSCALL_NO_USLEEP         (4)
#define SYSCALL_NO_TIME_STAMP     (5)
#define SYSCALL_NO_BRK            (6)
#define SYSCALL_NO_GETDIRENT      (7)
#define SYSCALL_NO_STAT           (8)
#define SYSCALL_NO_PROCESS_CREATE (9)
#define SYSCALL_NO_PROCESS_DELETE (10)
#define SYSCALL_NO_MY_ID          (11)
#define SYSCALL_NO_PROCESS_STATUS (12)
#define SYSCALL_NO_PROCESS_WAIT   (13)
#define SYSCALL_NO_MMAP           (14)
#define SYSCALL_NO_MUNMAP         (15)

#define SYSCALL_NO_UNIMPL     (100)

static int unimplemented_syscall() {
    printf("Calling unimplemented syscall, using placeholder id %d\n", SYSCALL_NO_UNIMPL);
    seL4_SetMR(0, SYSCALL_NO_UNIMPL);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 1));
    return seL4_GetMR(0);
}

int sos_sys_open(const char *path, fmode_t mode)
{
    seL4_SetMR(0, SYSCALL_NO_OPEN);
    seL4_SetMR(1, (seL4_Word) path);
    seL4_SetMR(2, strlen(path));
    seL4_SetMR(3, mode);
    // seL4_SetMR(3, 0);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_sys_close(int file)
{
    seL4_SetMR(0, SYSCALL_NO_CLOSE);
    seL4_SetMR(1, file);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 2));
    return seL4_GetMR(0);
}

int sos_sys_read(int file, char *buf, size_t nbyte)
{
    seL4_SetMR(0, SYSCALL_NO_READ);
    seL4_SetMR(1, file);
    seL4_SetMR(2, (seL4_Word) buf);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_sys_write(int file, const char *buf, size_t nbyte)
{
    seL4_SetMR(0, SYSCALL_NO_WRITE);
    seL4_SetMR(1, file);
    seL4_SetMR(2, (seL4_Word) buf);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    seL4_SetMR(0, SYSCALL_NO_GETDIRENT);
    seL4_SetMR(1, pos);
    seL4_SetMR(2, (seL4_Word) name);
    seL4_SetMR(3, nbyte);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    seL4_SetMR(0, SYSCALL_NO_STAT);
    seL4_SetMR(1, (seL4_Word) path);
    seL4_SetMR(2, strlen(path));
    seL4_SetMR(3, buf);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    return seL4_GetMR(0);
}

pid_t sos_process_create(const char *path)
{
    seL4_SetMR(0, SYSCALL_NO_PROCESS_CREATE);
    seL4_SetMR(1, (seL4_Word) path);
    seL4_SetMR(2, strlen(path));
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 3));
    return seL4_GetMR(0);
}

int sos_process_delete(pid_t pid)
{
    seL4_SetMR(0, SYSCALL_NO_PROCESS_DELETE);
    seL4_SetMR(1, pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 2));
    return seL4_GetMR(0);
}

pid_t sos_my_id(void)
{
    seL4_SetMR(0, SYSCALL_NO_MY_ID);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 1));
    return seL4_GetMR(0);
}

int sos_process_status(sos_process_t *processes, unsigned max)
{
    seL4_SetMR(0, SYSCALL_NO_PROCESS_STATUS);
    seL4_SetMR(1, (seL4_Word) processes);
    seL4_SetMR(2, max);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 3));
    return seL4_GetMR(0);
}

pid_t sos_process_wait(pid_t pid)
{
    seL4_SetMR(0, SYSCALL_NO_PROCESS_WAIT);
    seL4_SetMR(1, pid);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 2));
    return seL4_GetMR(0);
}

void sos_sys_usleep(int msec)
{
    seL4_SetMR(0, SYSCALL_NO_USLEEP);
    seL4_SetMR(1, msec);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 2));
}

int64_t sos_sys_time_stamp(void)
{
    seL4_SetMR(0, SYSCALL_NO_TIME_STAMP);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 1));
    return seL4_GetMR(0);
}

long sos_sys_brk(uintptr_t newbrk) {
    seL4_SetMR(0, SYSCALL_NO_BRK);
    seL4_SetMR(1, newbrk);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 2));
    return seL4_GetMR(0);
}

long sos_sys_mmap(uintptr_t vaddr, size_t len, int prot) {
    seL4_SetMR(0, SYSCALL_NO_MMAP);
    seL4_SetMR(1, vaddr);
    seL4_SetMR(2, len);
    seL4_SetMR(3, prot);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 4));
    long res = seL4_GetMR(0);
    return res;
}

long sos_sys_munmap(uintptr_t vaddr, size_t len) {
    seL4_SetMR(0, SYSCALL_NO_MUNMAP);
    seL4_SetMR(1, vaddr);
    seL4_SetMR(2, len);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, seL4_MessageInfo_new(0, 0, 0, 3));
    return seL4_GetMR(0);
}
