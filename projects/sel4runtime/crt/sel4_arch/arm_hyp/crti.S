/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */
.syntax unified

.section .init
.global _init
_init:
	stmdb sp!, {fp, lr}
	mov fp, sp

.section .fini
.global _fini
_fini:
	stmdb sp!, {fp, lr}
	mov fp, sp
