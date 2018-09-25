// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library - x86_64 backend
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

__ro_after_init struct mm_struct *wr_mm;
__ro_after_init unsigned long wr_base;

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;

struct mm_struct *copy_init_mm(void);
void __init wr_init(void)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long i;

	wr_mm = copy_init_mm();
	BUG_ON(!wr_mm);

	/*
	 * Place 64TB of kernel address space within 128TB of user address
	 * space, at a random page aligned offset.
	 */
	wr_base = (((unsigned long)kaslr_get_random_long("WR Poke")) &
		   PAGE_MASK) % (64 * _BITUL(40));

	/* Create alternate mapping for the entire wr_after_init range. */
	for (i = start; i < end; i += PAGE_SIZE) {
		struct page *page;
		spinlock_t *ptl;
		pte_t pte;
		pte_t *ptep;
		unsigned long wr_addr;

		page = virt_to_page(i);
		BUG_ON(!page);
		wr_addr = i + wr_base;

		/* The lock is not needed, but avoids open-coding. */
		ptep = get_locked_pte(wr_mm, wr_addr, &ptl);
		BUG_ON(!ptep);

		pte = mk_pte(page, PAGE_KERNEL);
		set_pte_at(wr_mm, wr_addr, ptep, pte);
		spin_unlock(ptl);
	}
}
