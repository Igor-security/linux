/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmem.h: Header for memory protection library - arm64 backend
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _ASM_ARM64_PRMEM_H
#define _ASM_ARM64_PRMEM_H

#include <linux/set_memory.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/mmu_context.h>
#include <asm/uaccess.h>

typedef struct {
       struct mm_struct *prev;
} temporary_mm_state_t;

static inline temporary_mm_state_t use_temporary_mm(struct mm_struct *mm)
{
       temporary_mm_state_t state;

       lockdep_assert_irqs_disabled();
       state.prev = current->active_mm;
       switch_mm_irqs_off(NULL, mm, current);
       return state;
}

static inline void unuse_temporary_mm(temporary_mm_state_t prev)
{
       lockdep_assert_irqs_disabled();
       switch_mm_irqs_off(NULL, prev.prev, current);
}


typedef temporary_mm_state_t wr_state_t;

extern __ro_after_init struct mm_struct *wr_mm;

static inline void *__wr_addr(void *addr)
{
	return addr;
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
	return (void *)raw_copy_to_user((void __user *)p, q, n);
}

#endif
