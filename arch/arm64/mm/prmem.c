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


__ro_after_init struct mm_struct *wr_poking_mm;

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;

__init unsigned long get_prmem_seed(void);
struct mm_struct *copy_init_mm(void);
void __init wr_poking_init(void)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long i;

	wr_poking_mm = copy_init_mm();
	BUG_ON(!wr_poking_mm);

	/* Create alternate mapping for the entire wr_after_init range. */
	for (i = start; i < end; i += PAGE_SIZE) {
		struct page *page;
		spinlock_t *ptl;
		pte_t pte;
		pte_t *ptep;

		page = virt_to_page(i);
		BUG_ON(!page);

		/* The lock is not needed, but avoids open-coding. */
		ptep = get_locked_pte(wr_poking_mm, i, &ptl);
		BUG_ON(!ptep);

		pte = mk_pte(page, PAGE_KERNEL);
		set_pte_at(wr_poking_mm, i, ptep, pte);
		spin_unlock(ptl);
	}
}
