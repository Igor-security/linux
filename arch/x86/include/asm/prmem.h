/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmem.h: Header for memory protection library - x86_64 backend
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _ASM_X86_PRMEM_H
#define _ASM_X86_PRMEM_H

#include <linux/set_memory.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/mmu_context.h>

typedef temporary_mm_state_t wr_state_t;

extern __ro_after_init struct mm_struct *wr_mm;
extern __ro_after_init unsigned long wr_base;

static inline void *__wr_addr(void *addr)
{
	return (void *)(wr_base + (unsigned long)addr);
}

static inline void __wr_enable(wr_state_t *state)
{
	*state = use_temporary_mm(wr_mm);
}

static inline void __wr_disable(wr_state_t *state)
{
	unuse_temporary_mm(*state);
}


/**
 * __wr_memset() - sets n bytes of the destination p to the c value
 * @p: beginning of the memory to write to
 * @c: byte to replicate
 * @n: amount of bytes to copy
 *
 * Returns pointer to the destination
 */
static inline void *__wr_memset(void *p, int c, __kernel_size_t n)
{
	return (void *)memset_user((void __user *)p, (u8)c, n);
}

/**
 * __wr_memcpy() - copyes n bytes from q to p
 * @p: beginning of the memory to write to
 * @q: beginning of the memory to read from
 * @n: amount of bytes to copy
 *
 * Returns pointer to the destination
 */
static inline void *__wr_memcpy(void *p, const void *q, __kernel_size_t n)
{
	return (void *)copy_to_user((void __user *)p, q, n);
}

#endif
