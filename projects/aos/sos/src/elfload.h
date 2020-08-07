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
#pragma once

#include <sel4/sel4.h>
#include <cspace/cspace.h>
#include <elf/elf.h>
#include <elf/elf32.h>
#include <elf/elf64.h>
#include <elf.h>

#include "vm/addrspace.h"
#include "vfs/vfs.h"
#include "coroutine/picoro.h"
#include "vm/pagetable.h"

int elf_load(cspace_t *cspace, process_t *proc, seL4_CPtr loadee_vspace, elf_t *elf_file, vnode_t *elf_vnode, addrspace_t *as, vaddr_t *end, bool pinned, coro_t coro);

int elf_getSectionNamed_v(elf_t *elfFile, vnode_t *vnode, const char *str, uintptr_t *result, coro_t coro);
int elf_find_vsyscall(elf_t *elfFile, vnode_t *vnode, uintptr_t *result, coro_t coro);
