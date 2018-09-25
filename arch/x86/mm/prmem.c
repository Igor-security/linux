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

static __ro_after_init bool wr_ready;
static __ro_after_init struct mm_struct *wr_poking_mm;
static __ro_after_init unsigned long wr_poking_base;

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;

static inline bool is_wr_after_init(unsigned long ptr, __kernel_size_t size)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long low = ptr;
	unsigned long high = ptr + size;

	return likely(start <= low && low <= high && high <= end);
}

void *__wr_op(unsigned long dst, unsigned long src, __kernel_size_t len,
	      enum wr_op_type op)
{
	temporary_mm_state_t prev;
	unsigned long wr_poking_addr;

	/* Confirm that the writable mapping exists. */
	if (WARN_ONCE(!wr_ready, "No writable mapping available"))
		return (void *)dst;

	if (WARN_ONCE(op >= WR_OPS_NUMBER, "Invalid WR operation.") ||
	    WARN_ONCE(!is_wr_after_init(dst, len), "Invalid WR range."))
		return (void *)dst;

	wr_poking_addr = wr_poking_base + dst;
	local_irq_disable();
	prev = use_temporary_mm(wr_poking_mm);

	if (op == WR_MEMCPY)
		copy_to_user((void __user *)wr_poking_addr, (void *)src, len);
	else if (op == WR_MEMSET)
		memset_user((void __user *)wr_poking_addr, (u8)src, len);

	unuse_temporary_mm(prev);
	local_irq_enable();
	return (void *)dst;
}

#define TB (1UL << 40)

struct mm_struct *copy_init_mm(void);
void __init wr_poking_init(void)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long i;
	unsigned long wr_range;

	wr_poking_mm = copy_init_mm();
	if (WARN_ONCE(!wr_poking_mm, "No alternate mapping available."))
		return;

	wr_range = round_up(end - start, PAGE_SIZE);

	/* Randomize the poking address base*/
	wr_poking_base = TASK_UNMAPPED_BASE +
		(kaslr_get_random_long("Write Rare Poking") & PAGE_MASK) %
		(TASK_SIZE - (TASK_UNMAPPED_BASE + wr_range));

	/*
	 * Place 64TB of kernel address space within 128TB of user address
	 * space, at a random page aligned offset.
	 */
	wr_poking_base = (((unsigned long)kaslr_get_random_long("WR Poke")) &
			  PAGE_MASK) % (64 * _BITUL(40));

	/* Create alternate mapping for the entire wr_after_init range. */
	for (i = start; i < end; i += PAGE_SIZE) {
		struct page *page;
		spinlock_t *ptl;
		pte_t pte;
		pte_t *ptep;
		unsigned long wr_poking_addr;

		page = virt_to_page(i);
		if (WARN_ONCE(!page, "WR memory without physical page"))
			return;
		wr_poking_addr = i + wr_poking_base;

		/* The lock is not needed, but avoids open-coding. */
		ptep = get_locked_pte(wr_poking_mm, wr_poking_addr, &ptl);
		if (WARN_ONCE(!ptep, "No pte for writable mapping"))
			return;

		pte = mk_pte(page, PAGE_KERNEL);
		set_pte_at(wr_poking_mm, wr_poking_addr, ptep, pte);
		spin_unlock(ptl);
	}
	wr_ready = true;
}
