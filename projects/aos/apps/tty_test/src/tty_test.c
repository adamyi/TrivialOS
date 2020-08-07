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

#define NPAGES 250
#define TEST_ADDRESS 0x8000000000

/* called from pt_test */
static void
do_pt_test(char *buf)
{
    int i;

    /* set */
    for (int i = 0; i < NPAGES; i++) {
      buf[i * PAGE_SIZE_4K] = i;
    }

    /* check */
    for (int i = 0; i < NPAGES; i++) {
      assert(buf[i * PAGE_SIZE_4K] == i);
    }
}

static void
pt_test( void )
{
#if 0
    /* need a decent sized stack */
    char buf1[NPAGES * PAGE_SIZE_4K], *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) TEST_ADDRESS);

    /* stack test */
    do_pt_test(buf1);
#endif 
    printf("pt_test\n");

    /* heap test */
    
    char *buf2 = malloc(NPAGES * PAGE_SIZE_4K);
    printf("buf2 %p\n", buf2);
    assert(buf2);
    do_pt_test(buf2);
    free(buf2);
    char *buf3 = malloc(NPAGES * PAGE_SIZE_4K);
    char *buf4 = malloc(NPAGES * PAGE_SIZE_4K);
    char *buf5 = malloc(NPAGES * PAGE_SIZE_4K);
    assert(buf3);
    assert(buf4);
    assert(buf5);
    do_pt_test(buf3);
    do_pt_test(buf4);
    do_pt_test(buf5);
    free(buf4);
    free(buf3);
    free(buf5);
    

    // mmap 10 pages and munmap them in an annoying order
    void *x = mmap(0, 5*PAGE_SIZE_4K, PROT_NONE, MAP_SHARED, 0, 0); // 0-4
    void *y = mmap(x + 5 *PAGE_SIZE_4K, 5*PAGE_SIZE_4K, PROT_NONE, MAP_SHARED, 0, 0); // 5-9
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

    printf("hello\n");

/*
    pt_test();
    return 0;
    */
    
    printf("hello\n");

    int pid = sos_my_id();

    int i = 0;

    do {
        printf("%d [%d]:Hello world\n", pid, i);
        // thread_block();
        printf("calling sleep\n");
        sleep(1);    // Implement this as a syscall
        printf("slept\n");
        i++;
    } while (1);

    exit(0);

    return 0;
}
