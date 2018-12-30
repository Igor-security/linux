// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library - generic part
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/mmu_context.h>
#include <linux/rcupdate.h>
#include <asm/prmem.h>

/*
 * Generic code, relying on the architecture code to provide:
 *  - type definition of wr_state
 *  - void __wr_enable(wr_state_t *state)
 *  - void *__wr_addr(void *addr)
 *  - void *__wr_memset(void *p, int c, __kernel_size_t len)
 *  - void *__wr_memcpy(void *p, const void *q, __kernel_size_t size)
 *  - void __wr_disable(wr_state_t *state)
 */

/*
 * The following two variables are statically allocated by the linker
 * script at the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;
static unsigned long start = (unsigned long)&__start_wr_after_init;
static unsigned long end = (unsigned long)&__end_wr_after_init;

static inline bool is_wr_after_init(void *p, __kernel_size_t n)
{
	unsigned long low = (unsigned long)p;
	unsigned long high = low + n;

	return likely(start <= low && high <= end);
}

#define wr_mem_is_writable() (system_state == SYSTEM_BOOTING)

/**
 * wr_memcpy() - copies n bytes from q to p
 * @p: beginning of the memory to write to
 * @q: beginning of the memory to read from
 * @n: amount of bytes to copy
 *
 * Returns pointer to the destination
 */
void *wr_memcpy(void *p, const void *q, __kernel_size_t n)
{
	wr_state_t wr_state;
	void *wr_addr;

	if (WARN_ONCE(!is_wr_after_init(p, n), "Invalid WR range."))
		return p;

	if (unlikely(wr_mem_is_writable()))
		return memcpy(p, q, n);

	wr_addr = __wr_addr(p);
	local_irq_disable();
	__wr_enable(&wr_state);
	__wr_memcpy(wr_addr, q, n);
	__wr_disable(&wr_state);
	local_irq_enable();
	return p;
}

/**
 * wr_memset() - sets n bytes of the destination p to the c value
 * @p: beginning of the memory to write to
 * @c: byte to replicate
 * @n: amount of bytes to copy
 *
 * Returns pointer to the destination
 */
void *wr_memset(void *p, int c, __kernel_size_t n)
{
	wr_state_t wr_state;
	void *wr_addr;

	if (WARN_ONCE(!is_wr_after_init(p, n), "Invalid WR range."))
		return p;

	if (unlikely(wr_mem_is_writable()))
		return memset(p, c, n);

	wr_addr = __wr_addr(p);
	local_irq_disable();
	__wr_enable(&wr_state);
	__wr_memset(wr_addr, c, n);
	__wr_disable(&wr_state);
	local_irq_enable();
	return p;
}
