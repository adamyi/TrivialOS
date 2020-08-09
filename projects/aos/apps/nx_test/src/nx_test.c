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

#define JUMP_TO_MAIN "\x20\x01\x1f\xd6" // BR x9

int prot = PROT_READ | PROT_WRITE | PROT_EXEC;

int main(void)
{
    sosapi_init_syscall_table();

    printf("nxtest: hello world from main()\n");

    char *test = mmap(0, PAGE_SIZE_4K, prot, MAP_ANONYMOUS, 0, 0);
    memcpy(test, JUMP_TO_MAIN, 4);

    if (prot & PROT_EXEC) {
        printf("jumping to region mmaped with PROT_EXEC, this should rerun main function\n");
    } else {
        printf("jumping to region mmaped without PROT_EXEC, this should trigger a permission fault\n");
    }
    prot = PROT_READ | PROT_WRITE;

    asm("LDR x9, =main");

    asm(
    "BR %0;"
    :
    :"r"(test)
    :);

    return 0;
}
