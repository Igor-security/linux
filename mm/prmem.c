// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library
 *
 * (C) Copyright 2017-2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/mmu_context.h>
#include <linux/rcupdate.h>
#include <linux/prmem.h>

__ro_after_init bool wr_ready;

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;
static unsigned long start = (unsigned long)&__start_wr_after_init;
static unsigned long end = (unsigned long)&__end_wr_after_init;

static inline bool is_wr_after_init(void *p, __kernel_size_t size)
{
	unsigned long low = (unsigned long)p;
	unsigned long high = low + size;

	return likely(start <= low && high <= end);
}

/**
 * wr_memcpy() - copyes size bytes from q to p
 * @p: beginning of the memory to write to
 * @q: beginning of the memory to read from
 * @size: amount of bytes to copy
 *
 * Returns pointer to the destination
 *
 * The architecture code must provide:
 *   void __wr_enable(wr_state_t *state)
 *   void *__wr_addr(void *addr)
 *   void *__wr_memcpy(void *p, const void *q, __kernel_size_t size)
 *   void __wr_disable(wr_state_t *state)
 */
void *wr_memcpy(void *p, const void *q, __kernel_size_t size)
{
	wr_state_t wr_state;
	void *wr_poking_addr = __wr_addr(p);

	if (WARN_ONCE(!wr_ready, "No writable mapping available") ||
	    WARN_ONCE(!is_wr_after_init(p, size), "Invalid WR range."))
		return p;

	local_irq_disable();
	__wr_enable(&wr_state);
	__wr_memcpy(wr_poking_addr, q, size);
	__wr_disable(&wr_state);
	local_irq_enable();
	return p;
}

/**
 * wr_memset() - sets len bytes of the destination p to the c value
 * @p: beginning of the memory to write to
 * @c: byte to replicate
 * @len: amount of bytes to copy
 *
 * Returns pointer to the destination
 *
 * The architecture code must provide:
 *   void __wr_enable(wr_state_t *state)
 *   void *__wr_addr(void *addr)
 *   void *__wr_memset(void *p, int c, __kernel_size_t len)
 *   void __wr_disable(wr_state_t *state)
 */
void *wr_memset(void *p, int c, __kernel_size_t len)
{
	wr_state_t wr_state;
	void *wr_poking_addr = __wr_addr(p);

	if (WARN_ONCE(!wr_ready, "No writable mapping available") ||
	    WARN_ONCE(!is_wr_after_init(p, len), "Invalid WR range."))
		return p;

	local_irq_disable();
	__wr_enable(&wr_state);
	__wr_memset(wr_poking_addr, c, len);
	__wr_disable(&wr_state);
	local_irq_enable();
	return p;
}
