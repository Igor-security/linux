/* SPDX-License-Identifier: GPL-2.0 */
/* Atomic operations for write rare memory */
#ifndef _LINUX_PRATOMIC_LONG_H
#define _LINUX_PRATOMIC_LONG_H
#include <linux/prmem.h>
#include <linux/compiler.h>
#include <asm-generic/atomic-long.h>

struct pratomic_long_t {
	atomic_long_t l __aligned(sizeof(atomic_long_t));
} __aligned(sizeof(atomic_long_t));

#define PRATOMIC_LONG_INIT(i)	{	\
	.l = ATOMIC_LONG_INIT((i)),	\
}

static __always_inline
bool __pratomic_long_op(bool inc, struct pratomic_long_t *l)
{
	struct page *page;
	uintptr_t base;
	uintptr_t offset;
	unsigned long flags;
	size_t size = sizeof(*l);
	bool is_virt = __is_wr_after_init(l, size);

	if (WARN(!(is_virt || likely(__is_wr_pool(l, size))),
		 WR_ERR_RANGE_MSG))
		return false;
	local_irq_save(flags);
	if (is_virt)
		page = virt_to_page(l);
	else
		vmalloc_to_page(l);
	offset = (~PAGE_MASK) & (uintptr_t)l;
	base = (uintptr_t)vmap(&page, 1, VM_MAP, PAGE_KERNEL);
	if (WARN(!base, WR_ERR_PAGE_MSG)) {
		local_irq_restore(flags);
		return false;
	}
	if (inc)
		atomic_long_inc((atomic_long_t *)(base + offset));
	else
		atomic_long_dec((atomic_long_t *)(base + offset));
	vunmap((void *)base);
	local_irq_restore(flags);
	return true;

}

/**
 * pratomic_long_inc - atomic increment of rare write long
 * @l: address of the variable of type struct pratomic_long_t
 *
 * Return: true on success, false otherwise
 */
static __always_inline bool pratomic_long_inc(struct pratomic_long_t *l)
{
	return __pratomic_long_op(true, l);
}

/**
 * pratomic_long_inc - atomic decrement of rare write long
 * @l: address of the variable of type struct pratomic_long_t
 *
 * Return: true on success, false otherwise
 */
static __always_inline bool pratomic_long_dec(struct pratomic_long_t *l)
{
	return __pratomic_long_op(false, l);
}

#endif
