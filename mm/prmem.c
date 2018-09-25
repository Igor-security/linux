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
	unsigned long flags;
	unsigned long offset;
	unsigned long wr_poking_addr;

	/* Confirm that the writable mapping exists. */
	BUG_ON(!wr_ready);

	if (WARN_ONCE(op >= WR_OPS_NUMBER, "Invalid WR operation.") ||
	    WARN_ONCE(!is_wr_after_init(dst, len), "Invalid WR range."))
		return (void *)dst;

	offset = dst - (unsigned long)&__start_wr_after_init;
	wr_poking_addr = wr_poking_base + offset;
	local_irq_save(flags);
	prev = use_temporary_mm(wr_poking_mm);

	kasan_disable_current();
	if (op == WR_MEMCPY)
		memcpy((void *)wr_poking_addr, (void *)src, len);
	else if (op == WR_MEMSET)
		memset((u8 *)wr_poking_addr, (u8)src, len);
	else if (op == WR_RCU_ASSIGN_PTR)
		/* generic version of rcu_assign_pointer */
		smp_store_release((void **)wr_poking_addr,
				  RCU_INITIALIZER((void **)src));
	kasan_enable_current();

	barrier(); /* XXX redundant? */

	unuse_temporary_mm(prev);
	/* XXX make the verification optional? */
	if (op == WR_MEMCPY)
		BUG_ON(memcmp((void *)dst, (void *)src, len));
	else if (op == WR_MEMSET)
		BUG_ON(memtst((void *)dst, (u8)src, len));
	else if (op == WR_RCU_ASSIGN_PTR)
		BUG_ON(*(unsigned long *)dst != src);
	local_irq_restore(flags);
	return (void *)dst;
}

struct mm_struct *copy_init_mm(void);
void __init wr_poking_init(void)
{
	unsigned long start = (unsigned long)&__start_wr_after_init;
	unsigned long end = (unsigned long)&__end_wr_after_init;
	unsigned long i;
	unsigned long wr_range;

	wr_poking_mm = copy_init_mm();
	BUG_ON(!wr_poking_mm);

	/* XXX What if it's too large to fit in the task unmapped mem? */
	wr_range = round_up(end - start, PAGE_SIZE);

	/* Randomize the poking address base*/
	wr_poking_base = TASK_UNMAPPED_BASE +
		(kaslr_get_random_long("Write Rare Poking") & PAGE_MASK) %
		(TASK_SIZE - (TASK_UNMAPPED_BASE + wr_range));

	/* Create alternate mapping for the entire wr_after_init range. */
	for (i = start; i < end; i += PAGE_SIZE) {
		struct page *page;
		spinlock_t *ptl;
		pte_t pte;
		pte_t *ptep;
		unsigned long wr_poking_addr;

		BUG_ON(!(page = virt_to_page(i)));
		wr_poking_addr = i - start + wr_poking_base;

		/* The lock is not needed, but avoids open-coding. */
		ptep = get_locked_pte(wr_poking_mm, wr_poking_addr, &ptl);
		VM_BUG_ON(!ptep);

		pte = mk_pte(page, PAGE_KERNEL);
		set_pte_at(wr_poking_mm, wr_poking_addr, ptep, pte);
		spin_unlock(ptl);
	}
	wr_ready = true;
}
