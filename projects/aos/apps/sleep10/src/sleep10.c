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

#include <sos.h>

#include <utils/page.h>

int main(void)
{
    sosapi_init_syscall_table();

    int pid = sos_my_id();

    printf("sleep10: hello from %d, i'm gonna sleep for 10 sec and die\n", pid);

    sleep(10);

    printf("sleep10: bye from %d\n", pid);

    exit(0);

    return 0;
}
