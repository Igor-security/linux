/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmalloc_helpers.h: Protectable Memory Allocator internal header
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _MM_VMALLOC_HELPERS_H
#define _MM_VMALLOC_HELPERS_H

<<<<<<< current
#ifndef CONFIG_PROTECTABLE_MEMORY

static inline void check_pmalloc_object(const void *ptr, unsigned long n,
					bool to_user)
{
}

#else

#include <linux/set_memory.h>
struct pmalloc_pool {
	struct mutex mutex;
	struct list_head pool_node;
	struct llist_head vm_areas;
	size_t refill;
	size_t offset;
	size_t align;
	bool rewritable;
};

#define VM_PMALLOC_PROTECTED_MASK (VM_PMALLOC | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_REWRITABLE_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE)
#define VM_PMALLOC_PROTECTED_REWRITABLE_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE | VM_PMALLOC_PROTECTED)

static __always_inline unsigned long area_flags(struct vmap_area *area)
{
	return area->vm->flags & VM_PMALLOC_MASK;
}

static __always_inline void tag_area(struct vmap_area *area, bool rewritable)
{
	if (rewritable == PMALLOC_RW)
		area->vm->flags |= VM_PMALLOC_REWRITABLE_MASK;
	else
		area->vm->flags |= VM_PMALLOC;
}

static __always_inline void untag_area(struct vmap_area *area)
{
	area->vm->flags &= ~VM_PMALLOC_MASK;
}

static __always_inline struct vmap_area *current_area(struct pmalloc_pool *pool)
{
	return llist_entry(pool->vm_areas.first, struct vmap_area,
			   area_list);
}

static __always_inline bool area_matches_mask(struct vmap_area *area,
					      unsigned long mask)
{
	return (area->vm->flags & mask) == mask;
}

static __always_inline bool is_area_protected(struct vmap_area *area)
{
	return area_matches_mask(area, VM_PMALLOC_PROTECTED_MASK);
}

static __always_inline bool is_area_rewritable(struct vmap_area *area)
{
	return area_matches_mask(area, VM_PMALLOC_REWRITABLE_MASK);
}

static __always_inline void protect_area(struct vmap_area *area)
{
	if (unlikely(is_area_protected(area)))
		return;
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED_MASK;
}

static __always_inline void make_area_ro(struct vmap_area *area)
{
	area->vm->flags &= ~VM_PMALLOC_REWRITABLE;
	protect_area(area);
}

static __always_inline void unprotect_area(struct vmap_area *area)
{
	if (likely(is_area_protected(area)))
		set_memory_rw(area->va_start, area->vm->nr_pages);
	untag_area(area);
}

static __always_inline void destroy_area(struct vmap_area *area)
{
	WARN(!is_area_protected(area), "Destroying unprotected area.");
	unprotect_area(area);
	vfree((void *)area->va_start);
}

static __always_inline bool empty(struct pmalloc_pool *pool)
{
	return unlikely(llist_empty(&pool->vm_areas));
}

static __always_inline bool protected(struct pmalloc_pool *pool)
{
	return is_area_protected(current_area(pool));
}

static inline bool exhausted(struct pmalloc_pool *pool, size_t size)
{
	size_t space_before;
	size_t space_after;

	space_before = round_down(pool->offset, pool->align);
	space_after = pool->offset - space_before;
	return unlikely(space_after < size && space_before < size);
}

static __always_inline bool space_needed(struct pmalloc_pool *pool, size_t size)
{
	return empty(pool) || protected(pool) || exhausted(pool, size);
}

static __always_inline size_t get_area_pages_size(struct vmap_area *area)
{
	return area->vm->nr_pages * PAGE_SIZE;
}

static inline int is_pmalloc_object(const void *ptr, const unsigned long n)
{
	struct vm_struct *area;
	unsigned long start = (unsigned long)ptr;
	unsigned long end = start + n;
	unsigned long area_end;

	if (likely(!is_vmalloc_addr(ptr)))
		return false;

	area = vmalloc_to_page(ptr)->area;
	if (unlikely(!(area->flags & VM_PMALLOC)))
		return false;

	area_end = area->nr_pages * PAGE_SIZE + (unsigned long)area->addr;
	return (start <= end) && (end <= area_end);
}

void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len);

static inline void check_pmalloc_object(const void *ptr, unsigned long n,
					bool to_user)
{
	int retv;

	retv = is_pmalloc_object(ptr, n);
	if (unlikely(retv)) {
		if (unlikely(!to_user))
			usercopy_abort("pmalloc",
				       "trying to write to pmalloc object",
				       to_user, (const unsigned long)ptr, n);
		if (retv < 0)
			usercopy_abort("pmalloc",
				       "invalid pmalloc object",
				       to_user, (const unsigned long)ptr, n);
	}
}

static __always_inline size_t get_area_pages_end(struct vmap_area *area)
{
	return area->va_start + get_area_pages_size(area);
}

static __always_inline bool area_contains_range(struct vmap_area *area,
						const void *addr,
						size_t n_bytes)
{
	size_t area_end = get_area_pages_end(area);
	size_t range_start = (size_t)addr;
	size_t range_end = range_start + n_bytes;

	return (area->va_start <= range_start) &&
	       (range_start < area_end) &&
	       (area->va_start <= range_end) &&
	       (range_end <= area_end);
}

static __always_inline
struct vmap_area *pool_get_area(struct pmalloc_pool *pool,
				const void *addr, size_t n_bytes)
{
	struct vmap_area *area;

	llist_for_each_entry(area, pool->vm_areas.first, area_list)
		if (area_contains_range(area, addr,  n_bytes))
			return area;
	return NULL;
}

#endif
#endif
