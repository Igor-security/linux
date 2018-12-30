// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library - arm64 backend
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/prmem.h>

#include <linux/cache.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/types.h>

#include <asm/fixmap.h>
#include <asm/kernel-pgtable.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/uaccess.h>

__ro_after_init struct mm_struct *wr_mm;
/**
 * __wr_enable() - activates the alternate mapping, for write rare
 * @state: temporary storage for the mapping preceding the write rare
 */
void __wr_enable(wr_state_t *state)
{
	lockdep_assert_irqs_disabled();
	state->prev = current->active_mm;
	switch_mm_irqs_off(NULL, wr_mm, current);
}

/**
 * __wr_disable() - restores the mapping preceding the write rare
 * @state: temporary storage for the mapping preceding the write rare
 */
void __wr_disable(wr_state_t *state)
{
	lockdep_assert_irqs_disabled();
	switch_mm_irqs_off(NULL, state->prev, current);
}

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;

static bool __init __wr_map_address(unsigned long addr)
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

struct mm_struct *copy_init_mm(void);
void __init wr_init(void)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long addr;

	wr_mm = copy_init_mm();
	BUG_ON(!wr_mm);

	/* Create alternate mapping for the entire wr_after_init range. */
	for (addr = start; addr < end; addr += PAGE_SIZE)
		BUG_ON(!__wr_map_address(addr));
}
