// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library
 *
 * (C) Copyright 2018-2019 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/mmu_context.h>
#include <linux/uaccess.h>

struct wr_state {
	struct mm_struct *prev;
};


__ro_after_init struct mm_struct *wr_mm;
__ro_after_init unsigned long wr_base;

/*
 * Default implementation of arch-specific functionality.
 * Each arch can override the parts that require special handling.
 */
unsigned long __init __weak __init_wr_base(void)
{
	return 0UL;
}

void * __weak __wr_addr(void *addr)
{
	return (void *)(wr_base + (unsigned long)addr);
}

void __weak __wr_enable(struct wr_state *state)
{
	lockdep_assert_irqs_disabled();
	state->prev = current->active_mm;
	switch_mm_irqs_off(NULL, wr_mm, current);
}

void __weak __wr_disable(struct wr_state *state)
{
	lockdep_assert_irqs_disabled();
	switch_mm_irqs_off(NULL, state->prev, current);
}

bool __init __weak __wr_map_address(unsigned long addr)
{
	spinlock_t *ptl;
	pte_t pte;
	pte_t *ptep;
	unsigned long wr_addr;
	struct page *page = virt_to_page(addr);

	if (unlikely(!page))
		return false;
	wr_addr = (unsigned long)__wr_addr((void *)addr);

	/* The lock is not needed, but avoids open-coding. */
	ptep = get_locked_pte(wr_mm, wr_addr, &ptl);
	if (unlikely(!ptep))
		return false;

	pte = mk_pte(page, PAGE_KERNEL);
	set_pte_at(wr_mm, wr_addr, ptep, pte);
	spin_unlock(ptl);
	return true;
}

void * __weak __wr_memset(void *p, int c, __kernel_size_t n)
{
	return (void *)memset_user((void __user *)p, (u8)c, n);
}

void * __weak __wr_memcpy(void *p, const void *q, __kernel_size_t n)
{
	return (void *)copy_to_user((void __user *)p, q, n);
}

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
	struct wr_state state;
	void *wr_addr;

	if (WARN_ONCE(!is_wr_after_init(p, n), "Invalid WR range."))
		return p;

	if (unlikely(wr_mem_is_writable()))
		return memcpy(p, q, n);

	wr_addr = __wr_addr(p);
	local_irq_disable();
	__wr_enable(&state);
	__wr_memcpy(wr_addr, q, n);
	__wr_disable(&state);
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
	struct wr_state state;
	void *wr_addr;

	if (WARN_ONCE(!is_wr_after_init(p, n), "Invalid WR range."))
		return p;

	if (unlikely(wr_mem_is_writable()))
		return memset(p, c, n);

	wr_addr = __wr_addr(p);
	local_irq_disable();
	__wr_enable(&state);
	__wr_memset(wr_addr, c, n);
	__wr_disable(&state);
	local_irq_enable();
	return p;
}

struct mm_struct *copy_init_mm(void);
void __init wr_init(void)
{
	unsigned long addr;

	wr_mm = copy_init_mm();
	BUG_ON(!wr_mm);

	wr_base = __init_wr_base();

	/* Create alternate mapping for the entire wr_after_init range. */
	for (addr = start; addr < end; addr += PAGE_SIZE)
		BUG_ON(!__wr_map_address(addr));
}
