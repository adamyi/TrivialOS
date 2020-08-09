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
#include <unistd.h>
#include <sel4/sel4.h>
#include <syscalls.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <sos.h>

#include <utils/page.h>

int main(void)
{
    sosapi_init_syscall_table();

    int in = open("rdonly_test", O_RDONLY);
    assert(in >= 0);

    printf("i'm gonna try using read to write to read-only .code segment\n");

    assert(sos_sys_read(in, (char *) main, 10) < 0);

    printf("ok that failed as intended\n now i'm gonna write to .code and segfault. This test will be successful if this is the last printf\n");

    *((char *) main) = 0x88;

    printf("this line shouldn't be printed out - i should be killed!\n");

    return 0;
}
