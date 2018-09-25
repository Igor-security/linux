// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library
 *
 * (C) Copyright 2017-2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

//#include <linux/set_memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/mmu_context.h>
#include <linux/rcupdate.h>
#include <linux/prmem.h>

const char WR_ERR_RANGE_MSG[] = "Write rare on invalid memory range.";
const char WR_ERR_PAGE_MSG[] = "Failed to remap write rare page.";

static __ro_after_init bool wr_ready;
static __ro_after_init struct mm_struct *wr_poking_mm;
static __ro_after_init unsigned long wr_poking_addr;

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

	return likely(start <= low && low < high && high <= end);
}

static inline
void op_start(unsigned long dst, unsigned long *psrc, __kernel_size_t len,
		enum wr_op_type op)
{
	if (op == WR_MEMCPY)
		memcpy((void *)dst, (void *)*psrc, len);
	if (op == WR_MEMSET)
		memset((u8 *)dst, (u8)*psrc, len);
	if (op == WR_RCU_ASSIGN_PTR)
		/*
		 * Use the version of rcu_assign_pointer not optimized
		 * for constants
		 */
		smp_store_release((void **)dst,
				  RCU_INITIALIZER((void **)*psrc));
}

static inline
void op_end(unsigned long dst, unsigned long *psrc, __kernel_size_t len,
	      enum wr_op_type op)
{
	if (op == WR_MEMCPY) {
		BUG_ON(memcmp((void *)dst, (void *)*psrc, len));
		*psrc += len;
	} else if (op == WR_MEMSET) {
		BUG_ON(memtst((void *)dst, (int)*psrc, len));
	} else if (op == WR_RCU_ASSIGN_PTR) {
		BUG_ON(*(unsigned long *)dst != *psrc);
	}
}

static inline
void op_page(unsigned long dst, unsigned long *psrc,
	     __kernel_size_t len, enum wr_op_type op)
{
	temporary_mm_state_t prev;
	unsigned long flags;
	pte_t pte, *ptep;
	spinlock_t *ptl;
	struct page *page = virt_to_page(dst);

	BUG_ON(!page);
	local_irq_save(flags);

	/* The lock is not needed, but avoids open-coding. */
	ptep = get_locked_pte(wr_poking_mm, wr_poking_addr, &ptl);
	VM_BUG_ON(!ptep); /* preallocated in poking_init() */

	pte = mk_pte(page, PAGE_KERNEL);
	set_pte_at(wr_poking_mm, wr_poking_addr, ptep, pte);
	/* Loading the mm will set the PTE before the operation. */
	prev = use_temporary_mm(wr_poking_mm);

	kasan_disable_current();
	op_start(wr_poking_addr + offset_in_page(dst), psrc, len, op);
	kasan_enable_current();

	barrier();
	/* Only after the memory operation, clear the PTE. */
	pte_clear(wr_poking_mm, wr_poking_addr, ptep);

	/*
	 * Flushes also the "user" address spaces, even if not
	 * existent, with PTI on. The PTE is flushed only on the
	 * core where it exists and it is done with a "user" function,
	 * even if the PTE is supervisor-only and global.
	 */
	__flush_tlb_one_user(wr_poking_addr);

	/*
	 * Loading the previous page-table hierarchy requires a
	 * serializing instruction that already allows the core to
	 * see the updated version. Xen-PV is assumed to serialize
	 * execution in a similar manner.
	 */
	unuse_temporary_mm(prev);

	pte_unmap_unlock(ptep, ptl);
	op_end(dst, psrc, len, op);
	local_irq_restore(flags);
}

void *__wr_op(unsigned long dst, unsigned long src, __kernel_size_t len,
	      enum wr_op_type op)
{
	unsigned long size;
	unsigned long p = dst;

	/* Confirm that it's past initialization. */
	BUG_ON(!wr_ready);

	if (WARN_ONCE(op >= WR_OPS_NUMBER,
		       "Invalid Write Rare operation") ||
	    WARN_ONCE(!is_wr_after_init(dst, len),
		      "Write rare on invalid memory range."))
		return (void *)dst;

	for (p = (unsigned long)dst; len; p += size, len -= size) {
		size = min(len, PAGE_SIZE - offset_in_page(p));
		op_page(p, &src, size, op);
	}
	return (void *)dst;
}

struct mm_struct *copy_init_mm(void);
void __init wr_poking_init(void)
{
	spinlock_t *ptl;
	pte_t *ptep;

	wr_poking_mm = copy_init_mm();
	BUG_ON(!wr_poking_mm);

	/* Randomize the poking address */
	wr_poking_addr = TASK_UNMAPPED_BASE +
		(kaslr_get_random_long("Write Rare Poking") & PAGE_MASK) %
		(TASK_SIZE - TASK_UNMAPPED_BASE - PAGE_SIZE);

	/*
	 * We need to trigger the allocation of the page-tables that will be
	 * needed for poking now. Later, poking may be performed in an atomic
	 * section, which might cause allocation to fail.
	 */
	ptep = get_locked_pte(wr_poking_mm, wr_poking_addr, &ptl);
	BUG_ON(!ptep);
	pte_unmap_unlock(ptep, ptl);
	wr_ready = true;
}
