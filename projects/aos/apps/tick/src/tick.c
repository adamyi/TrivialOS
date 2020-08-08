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
#include <syscalls.h>

#include <sos.h>

#include <utils/page.h>

#define SLEEP_INTERVAL 1

#define TICK_TIMES -1

// #define DEBUG

int main(void)
{
    sosapi_init_syscall_table();


    int pid = sos_my_id();

    printf("hello from %d: this is a ticking program that prints Hello every %d seconds\n", pid, SLEEP_INTERVAL);

    int i = 0;

    do {
        printf("%d [%d] :tick\n", pid, i);
#ifdef DEBUG
        printf("calling sleep\n");
#endif
        sleep(SLEEP_INTERVAL);
#ifdef DEBUG
        printf("slept\n");
#endif
        i++;
    } while (i != TICK_TIMES);

    exit(0);

    return 0;
}
