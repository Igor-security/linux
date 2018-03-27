// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library
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

#include <linux/prmem.h>

static LIST_HEAD(pools_list);
static DEFINE_MUTEX(pools_mutex);

#define MAX_ALIGN_ORDER (ilog2(sizeof(void *)))


/* Various helper functions. Inlined, to reduce the attack surface. */

static __always_inline void protect_area(struct vmap_area *area)
{
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED_MASK;
}

static __always_inline bool empty(struct pmalloc_pool *pool)
{
	return unlikely(!pool->area);
}

/* Allocation from a protcted area is allowed only for a START_WR pool. */
static __always_inline bool unwritable(struct pmalloc_pool *pool)
{
	return  (pool->area->vm->flags & VM_PMALLOC_PROTECTED) &&
		!((pool->area->vm->flags & VM_PMALLOC_WR) &&
		  (pool->mode & PMALLOC_START));
}

static __always_inline
bool exhausted(struct pmalloc_pool *pool, size_t size)
{
	size_t space_before;
	size_t space_after;

	space_before = round_down(pool->offset, pool->align);
	space_after = pool->offset - space_before;
	return unlikely(space_after < size && space_before < size);
}

static __always_inline
bool space_needed(struct pmalloc_pool *pool, size_t size)
{
	return empty(pool) || unwritable(pool) || exhausted(pool, size);
}

/**
 * pmalloc_init_custom_pool() - initialize a protectable memory pool
 * @pool: the pointer to the struct pmalloc_pool to initialize
 * @refill: the minimum size to allocate when in need of more memory.
 *          It will be rounded up to a multiple of PAGE_SIZE
 *          The value of 0 gives the default amount of PAGE_SIZE.
 * @align_order: log2 of the alignment to use when allocating memory
 *               Negative values give ARCH_KMALLOC_MINALIGN
 * @mode: is the data RO or RareWrite and should be provided already in
 *        protected mode.
 *
 * Initializes an empty memory pool, for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 */
void pmalloc_init_custom_pool(struct pmalloc_pool *pool, size_t refill,
			      short align_order, uint8_t mode)
{
	mutex_init(&pool->mutex);
	pool->area = NULL;
	if (align_order < 0)
		pool->align = ARCH_KMALLOC_MINALIGN;
	else
		pool->align = 1UL << align_order;
	pool->refill = refill ? PAGE_ALIGN(refill) :
				PMALLOC_DEFAULT_REFILL_SIZE;
	mode &= PMALLOC_MASK;
	if (mode & PMALLOC_START)
		mode |= PMALLOC_WR;
	pool->mode = mode & PMALLOC_MASK;
	pool->offset = 0;
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
						short align_order,
						uint8_t mode)
{
	struct pmalloc_pool *pool;

	pool = kmalloc(sizeof(struct pmalloc_pool), GFP_KERNEL);
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
	if (pool->mode & PMALLOC_WR)
		tag_mask |= VM_PMALLOC_WR;
	new_area->vm->flags |= (tag_mask & VM_PMALLOC_MASK);
	new_area->pool = pool;
	if (pool->mode & PMALLOC_START)
		protect_area(new_area);
	if (pool->mode & PMALLOC_AUTO && !empty(pool))
		protect_area(pool->area);
	/* The area size backed by pages, without the canary bird. */
	pool->offset = new_area->vm->nr_pages * PAGE_SIZE;
	new_area->next = pool->area;
	pool->area = new_area;
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
 * Allocation happens with a mutex locked, therefore it is assumed to have
 * exclusive write access to both the pool structure and the list of
 * vmap_areas, while inside the lock.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- error
 */
void *pmalloc(struct pmalloc_pool *pool, size_t size)
{
	void *retval = NULL;

	mutex_lock(&pool->mutex);
	if (unlikely(space_needed(pool, size)) &&
	    unlikely(grow(pool, size) != 0))
		goto error;
	pool->offset = round_down(pool->offset - size, pool->align);
	retval = (void *)(pool->area->va_start + pool->offset);
error:
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
	for (area = pool->area; area; area = area->next)
		protect_area(area);
	mutex_unlock(&pool->mutex);
}
EXPORT_SYMBOL(pmalloc_protect_pool);


/**
 * pmalloc_make_pool_ro() - forces a pool to become read-only
 * @pool: the pool associated to the memory to make ro
 *
 * Drops the possibility to perform controlled writes from both the pool
 * metadata and all the vm_area structures associated to the pool.
 * In case the pool was configured to automatically protect memory when
 * allocating it, the configuration is dropped.
 */
void pmalloc_make_pool_ro(struct pmalloc_pool *pool)
{
	struct vmap_area *area;

	mutex_lock(&pool->mutex);
	pool->mode &= ~(PMALLOC_WR | PMALLOC_START);
	for (area = pool->area; area; area = area->next)
		protect_area(area);
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

	mutex_lock(&pools_mutex);
	list_del(&pool->pool_node);
	mutex_unlock(&pools_mutex);
	while (pool->area) {
		area = pool->area;
		pool->area = area->next;
		set_memory_rw(area->va_start, area->vm->nr_pages);
		area->vm->flags &= ~VM_PMALLOC_MASK;
		vfree((void *)area->va_start);
	}
	kfree(pool);
}
EXPORT_SYMBOL(pmalloc_destroy_pool);
