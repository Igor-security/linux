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

static LIST_HEAD(pools_list);
static DEFINE_MUTEX(pools_mutex);

#define MAX_ALIGN_ORDER (ilog2(sizeof(void *)))
#define DEFAULT_REFILL_SIZE PAGE_SIZE

/**
 * pmalloc_init_custom_pool() - initialize a protectable memory pool
 * @pool: the pointer to the struct pmalloc_pool to initialize
 * @refill: the minimum size to allocate when in need of more memory.
 *          It will be rounded up to a multiple of PAGE_SIZE
 *          The value of 0 gives the default amount of PAGE_SIZE.
 * @align_order: log2 of the alignment to use when allocating memory
 *               Negative values give ARCH_KMALLOC_MINALIGN
 * @mode: can the data be altered after protection
 *
 * Initializes an empty memory pool, for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 */
void pmalloc_init_custom_pool(struct pmalloc_pool *pool, size_t refill,
			      unsigned short align_order, uint8_t mode)
{
	pool->refill = refill ? PAGE_ALIGN(refill) : DEFAULT_REFILL_SIZE;
	pool->mode = mode;
	pool->align = 1UL << align_order;
	mutex_init(&pool->mutex);
	mutex_lock(&pools_mutex);
	list_add(&pool->pool_node, &pools_list);
	mutex_unlock(&pools_mutex);
}
EXPORT_SYMBOL(pmalloc_init_custom_pool);

/**
 * pmalloc_create_custom_pool() - create a new protectable memory pool
 * @refill: the minimum size to allocate when in need of more memory.
 *          It will be rounded up to a multiple of PAGE_SIZE
 *          The value of 0 gives the default amount of PAGE_SIZE.
 * @align_order: log2 of the alignment to use when allocating memory
 *               Negative values give ARCH_KMALLOC_MINALIGN
 * @mode: can the data be altered after protection
 *
 * Creates a new (empty) memory pool for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
struct pmalloc_pool *pmalloc_create_custom_pool(size_t refill,
						unsigned short align_order,
						uint8_t mode)
{
	struct pmalloc_pool *pool;

	pool = kzalloc(sizeof(struct pmalloc_pool), GFP_KERNEL);
	if (WARN(!pool, "Could not allocate pool meta data."))
		return NULL;
	pmalloc_init_custom_pool(pool, refill, align_order, mode);
	return pool;
}
EXPORT_SYMBOL(pmalloc_create_custom_pool);

static int grow(struct pmalloc_pool *pool, size_t min_size)
{
	void *addr;
	struct vmap_area *new_area;
	unsigned long size;
	uint32_t tag_mask;

	size = (min_size > pool->refill) ? min_size : pool->refill;
	addr = vmalloc(size);
	if (WARN(!addr, "Failed to allocate %zd bytes", PAGE_ALIGN(size)))
		return -ENOMEM;

	new_area = find_vmap_area((unsigned long)addr);
	tag_mask = VM_PMALLOC;
	if (pool->mode & PMALLOC_RW)
		tag_mask |= VM_PMALLOC_REWRITABLE;
	__tag_area(new_area, tag_mask);
	if (pool->mode == PMALLOC_AUTO_RW)
		__protect_area(new_area);
	if (pool->mode == PMALLOC_AUTO_RO) {
		struct vmap_area *old_area;

		old_area = container_of(pool->vm_areas.first,
					struct vmap_area, area_list);
		__protect_area(old_area);
	}
	pool->offset = __get_area_pages_size(new_area);
	llist_add(&new_area->area_list, &pool->vm_areas);
	return 0;
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
	void *retval = NULL;

	mutex_lock(&pool->mutex);
	if (unlikely(__space_needed(pool, size)) &&
	    unlikely(grow(pool, size)))
		goto out;
	pool->offset = round_down(pool->offset - size, pool->align);
	retval = (void *)(__current_area(pool)->va_start + pool->offset);
out:
	mutex_unlock(&pool->mutex);
	return retval;
}
EXPORT_SYMBOL(pmalloc);

/**
 * pmalloc_protect_pool() - write-protects the memory in the pool
 * @pool: the pool associated to the memory to write-protect
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
		__protect_area(area);
	mutex_unlock(&pool->mutex);
}
EXPORT_SYMBOL(pmalloc_protect_pool);


/**
 * pmalloc_make_pool_ro() - drops rare-write permission from a pool
 * @pool: the pool associated to the memory to make ro
 *
 * Drops the possibility to perform controlled writes from both the pool
 * metadata and all the vm_area structures associated to the pool.
 */
void pmalloc_make_pool_ro(struct pmalloc_pool *pool)
{
	struct vmap_area *area;

	mutex_lock(&pool->mutex);
	pool->mode = false;
	llist_for_each_entry(area, pool->vm_areas.first, area_list)
		__protect_area(area);
	mutex_unlock(&pool->mutex);
}
EXPORT_SYMBOL(pmalloc_make_pool_ro);

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
		__destroy_area(area);
	}
}
EXPORT_SYMBOL(pmalloc_destroy_pool);
