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
/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 test.
 *
 *      Author:         Godfrey van der Linden
 *      Original Author:    Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <syscalls.h>
#include <sys/mman.h>

#include <sos.h>

#include <utils/page.h>

#define NPAGES_BRK 10
#define NPAGES_MMAP 250
#define NPAGES_STACK NPAGES_MMAP
#define TEST_ADDRESS 0x8000000000

/* called from pt_test */
static void
do_pt_test(int npages, char *buf)
{
    int i;

    /* set */
    for (int i = 0; i < npages; i++) {
      buf[i * PAGE_SIZE_4K] = i;
    }

    /* check */
    for (int i = 0; i < npages; i++) {
      assert(buf[i * PAGE_SIZE_4K] == i);
    }
}

static void
pt_test( void )
{
    /* need a decent sized stack */
    char buf1[NPAGES_STACK * PAGE_SIZE_4K];

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) TEST_ADDRESS);

    /* stack test */
    printf("stack test\n");
    do_pt_test(NPAGES_STACK, buf1);

    /* heap test (brk) */
    printf("heap test (brk)\n");
    char *buf2 = malloc(NPAGES_BRK * PAGE_SIZE_4K);
    assert(buf2);
    do_pt_test(NPAGES_BRK, buf2);
    free(buf2);
    
    /* heap test (mmap) */
    printf("heap test (mmap)\n");
    buf2 = malloc(NPAGES_MMAP * PAGE_SIZE_4K);
    assert(buf2);
    do_pt_test(NPAGES_MMAP, buf2);
    free(buf2);
    char *buf3 = malloc(NPAGES_MMAP * PAGE_SIZE_4K);
    char *buf4 = malloc(NPAGES_MMAP * PAGE_SIZE_4K);
    char *buf5 = malloc(NPAGES_MMAP * PAGE_SIZE_4K);
    assert(buf3);
    assert(buf4);
    assert(buf5);
    do_pt_test(NPAGES_MMAP, buf3);
    do_pt_test(NPAGES_MMAP, buf4);
    do_pt_test(NPAGES_MMAP, buf5);
    free(buf4);
    free(buf3);
    free(buf5);
    
    /* mmap test */
    // mmap 10 pages and munmap them in an annoying order
    printf("mmap test\n");
    void *x = mmap(0, 5*PAGE_SIZE_4K, PROT_NONE, MAP_ANONYMOUS, 0, 0); // 0-4
    void *y = mmap(x + 5 *PAGE_SIZE_4K, 5*PAGE_SIZE_4K, PROT_NONE, MAP_ANONYMOUS, 0, 0); // 5-9
    assert(x);
    assert(y);
    assert(y == x + 5 * PAGE_SIZE_4K);
    assert(0 == munmap(x+3*PAGE_SIZE_4K, 4 * PAGE_SIZE_4K)); // 3-6
    assert(munmap(x, 10*PAGE_SIZE_4K) < 0);
    assert(munmap(x, 5*PAGE_SIZE_4K) < 0);
    assert(munmap(x+3*PAGE_SIZE_4K, PAGE_SIZE_4K) < 0);
    assert(munmap(x+6*PAGE_SIZE_4K, 3*PAGE_SIZE_4K) < 0);
    assert(0 == munmap(x+PAGE_SIZE_4K, PAGE_SIZE_4K)); // 1
    assert(0 == munmap(x, PAGE_SIZE_4K)); // 0
    assert(0 == munmap(x+2*PAGE_SIZE_4K, PAGE_SIZE_4K)); // 2
    assert(0 == munmap(y+2*PAGE_SIZE_4K, 3*PAGE_SIZE_4K)); // 7-9
}

int main(void)
{
    sosapi_init_syscall_table();
    
    int pid = sos_my_id();

    printf("mem test init\n");
    pt_test();
    printf("mem test finished\n");

    exit(0);
    return 0;
}
