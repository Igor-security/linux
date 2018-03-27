// SPDX-License-Identifier: GPL-2.0
/*
 * pmalloc.c: Protectable Memory Allocator
 *
 * (C) Copyright 2017-2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/printk.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/set_memory.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/llist.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

#include <linux/pmalloc.h>

#define MAX_ALIGN_ORDER (ilog2(sizeof(void *)))
struct pmalloc_pool {
	struct mutex mutex;
	struct list_head pool_node;
	struct llist_head vm_areas;
	unsigned long refill;
	unsigned long offset;
	unsigned long align;
};

static LIST_HEAD(pools_list);
static DEFINE_MUTEX(pools_mutex);

static inline void tag_area(struct vmap_area *area)
{
	area->vm->flags |= VM_PMALLOC;
}

static inline void untag_area(struct vmap_area *area)
{
	area->vm->flags &= ~VM_PMALLOC;
}

static inline struct vmap_area *current_area(struct pmalloc_pool *pool)
{
	return llist_entry(pool->vm_areas.first, struct vmap_area,
			   area_list);
}

static inline bool is_area_protected(struct vmap_area *area)
{
	return area->vm->flags & VM_PMALLOC_PROTECTED;
}

static inline bool protect_area(struct vmap_area *area)
{
	if (unlikely(is_area_protected(area)))
		return false;
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED;
	return true;
}

static inline void destroy_area(struct vmap_area *area)
{
	WARN(!is_area_protected(area), "Destroying unprotected area.");
//	set_memory_rw(area->va_start, area->vm->nr_pages); //XXX Skip it?
	vfree((void *)area->va_start);
}

static inline bool empty(struct pmalloc_pool *pool)
{
	return unlikely(llist_empty(&pool->vm_areas));
}

static inline bool protected(struct pmalloc_pool *pool)
{
	return is_area_protected(current_area(pool));
}

static inline bool exhausted(struct pmalloc_pool *pool, size_t size)
{
	unsigned long space_before;
	unsigned long space_after;

	space_before = round_down(pool->offset, pool->align);
	space_after = pool->offset - space_before;
	return unlikely(space_after < size && space_before < size);
}

static inline bool space_needed(struct pmalloc_pool *pool, size_t size)
{
	return empty(pool) || protected(pool) || exhausted(pool, size);
}

#define DEFAULT_REFILL_SIZE PAGE_SIZE
/**
 * pmalloc_create_custom_pool() - create a new protectable memory pool
 * @refill: the minimum size to allocate when in need of more memory.
 *          It will be rounded up to a multiple of PAGE_SIZE
 *          The value of 0 gives the default amount of PAGE_SIZE.
 * @align_order: log2 of the alignment to use when allocating memory
 *               Negative values give ARCH_KMALLOC_MINALIGN
 *
 * Creates a new (empty) memory pool for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
struct pmalloc_pool *pmalloc_create_custom_pool(unsigned long refill,
						unsigned short align_order)
{
	struct pmalloc_pool *pool;

	pool = kzalloc(sizeof(struct pmalloc_pool), GFP_KERNEL);
	if (WARN(!pool, "Could not allocate pool meta data."))
		return NULL;

	pool->refill = refill ? PAGE_ALIGN(refill) : DEFAULT_REFILL_SIZE;
	pool->align = 1UL << align_order;
	mutex_init(&pool->mutex);

	mutex_lock(&pools_mutex);
	list_add(&pool->pool_node, &pools_list);
	mutex_unlock(&pools_mutex);
	return pool;
}


static int grow(struct pmalloc_pool *pool, size_t size)
{
	void *addr;
	struct vmap_area *area;

	addr = vmalloc(max(size, pool->refill));
	if (WARN(!addr, "Failed to allocate %zd bytes", PAGE_ALIGN(size)))
		return -ENOMEM;

	area = find_vmap_area((unsigned long)addr);
	tag_area(area);
	pool->offset = area->vm->nr_pages * PAGE_SIZE;
	llist_add(&area->area_list, &pool->vm_areas);
	return 0;
}

static unsigned long reserve_mem(struct pmalloc_pool *pool, size_t size)
{
	pool->offset = round_down(pool->offset - size, pool->align);
	return current_area(pool)->va_start + pool->offset;

}

/**
 * pmalloc() - allocate protectable memory from a pool
 * @pool: handle to the pool to be used for memory allocation
 * @size: amount of memory (in bytes) requested
 *
 * Allocates memory from a pool.
 * If needed, the pool will automatically allocate enough memory to
 * either satisfy the request or meet the "refill" parameter received
 * upon creation.
 * New allocation can happen also if the current memory in the pool is
 * already write protected.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- error
 */
void *pmalloc(struct pmalloc_pool *pool, size_t size)
{
	unsigned long retval = 0;

	mutex_lock(&pool->mutex);
	if (unlikely(space_needed(pool, size)) &&
	    unlikely(grow(pool, size)))
			goto out;
	retval = reserve_mem(pool, size);
out:
	mutex_unlock(&pool->mutex);
	return (void *)retval;
}

/**
 * pmalloc_protect_pool() - write-protects the memory in the pool
 * @pool: the pool associated tothe memory to write-protect
 *
 * Write-protects all the memory areas currently assigned to the pool
 * that are still unprotected.
 * This does not prevent further allocation of additional memory, that
 * can be initialized and protected.
 * The catch is that protecting a pool will make unavailable whatever
 * free memory it might still contain.
 * Successive allocations will grab more free pages.
 */
void pmalloc_protect_pool(struct pmalloc_pool *pool)
{
	struct vmap_area *area;

	mutex_lock(&pool->mutex);
	llist_for_each_entry(area, pool->vm_areas.first, area_list)
		if (unlikely(!protect_area(area)))
			break;
	mutex_unlock(&pool->mutex);
}


/**
 * is_pmalloc_object() - test if the given range is within a pmalloc pool
 * @ptr: the base address of the range
 * @n: the size of the range
 *
 * Return:
 * * true	- the range given is fully within a pmalloc pool
 * * false	- the range given is not fully within a pmalloc pool
 */
int is_pmalloc_object(const void *ptr, const unsigned long n)
{
	struct vm_struct *area;

	if (likely(!is_vmalloc_addr(ptr)))
		return false;

	area = vmalloc_to_page(ptr)->area;
	if (unlikely(!(area->flags & VM_PMALLOC)))
		return false;

	return ((n + (unsigned long)ptr) <=
		(area->nr_pages * PAGE_SIZE + (unsigned long)area->addr));

}


/**
 * pmalloc_destroy_pool() - destroys a pool and all the associated memory
 * @pool: the pool to destroy
 *
 * All the memory associated to the pool will be freed, including the
 * metadata used for the pool.
 */
void pmalloc_destroy_pool(struct pmalloc_pool *pool)
{
	struct vmap_area *area;
	struct llist_node *cursor;
	struct llist_node *tmp;

	mutex_lock(&pools_mutex);
	list_del(&pool->pool_node);
	mutex_unlock(&pools_mutex);

	cursor = pool->vm_areas.first;
	kfree(pool);
	while (cursor) {            /* iteration over llist */
		tmp = cursor;
		cursor = cursor->next;
		area = llist_entry(tmp, struct vmap_area, area_list);
		destroy_area(area);
	}
}
