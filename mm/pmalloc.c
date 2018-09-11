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


/*
 * Various helper functions. Inlined, to reduce the attack surface.
 */

static __always_inline void __protect_area(struct vmap_area *area)
{
	WARN(__is_area_protected(area),
	     "Attempting to protect already protected area %pK", area);
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED_MASK;
}

static __always_inline void __make_area_ro(struct vmap_area *area)
{
	WARN(!__is_area_rewritable(area),
	     "Attempting to convert already ro area %pK", area);
	area->vm->flags &= ~VM_PMALLOC_REWRITABLE;
	__protect_area(area);
}

static __always_inline void __unprotect_area(struct vmap_area *area)
{
	WARN(!__is_area_protected(area),
	     "Attempting to unprotect already unprotected area %pK", area);
	set_memory_rw(area->va_start, area->vm->nr_pages);
	__untag_area(area);
}

static __always_inline void __destroy_area(struct vmap_area *area)
{
	WARN(!__is_area_protected(area), "Destroying unprotected area.");
	__unprotect_area(area);
	vfree((void *)area->va_start);
}

static __always_inline bool __protected(struct pmalloc_pool *pool)
{
	return __is_area_protected(__current_area(pool));
}

static __always_inline bool __empty(struct pmalloc_pool *pool)
{
	return unlikely(llist_empty(&pool->vm_areas));
}

/*
 * The only case where it's allowed to allocate memory from a protected
 * area is when the area is both rewritable and configured to start
 * protected, when created.
 */
static __always_inline bool __unwritable(struct pmalloc_pool *pool)
{
	unsigned long area_flags = __area_flags(__current_area(pool));

	return  (area_flags & VM_PMALLOC_PROTECTED) &&
		!((area_flags & VM_PMALLOC_REWRITABLE) &&
		  (pool->mode & PMALLOC_POOL_START_PROTECTED));
}

static inline bool __exhausted(struct pmalloc_pool *pool, size_t size)
{
	size_t space_before;
	size_t space_after;

	space_before = round_down(pool->offset, pool->align);
	space_after = pool->offset - space_before;
	return unlikely(space_after < size && space_before < size);
}

static __always_inline
bool __space_needed(struct pmalloc_pool *pool, size_t size)
{
	return __empty(pool) || __unwritable(pool) || __exhausted(pool, size);
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
	init_llist_head(&pool->vm_areas);
	if (align_order < 0)
		pool->align = ARCH_KMALLOC_MINALIGN;
	else
		pool->align = 1UL << align_order;
	pool->refill = refill ? PAGE_ALIGN(refill) :
				PMALLOC_DEFAULT_REFILL_SIZE;
	pool->mode = mode & PMALLOC_POOL_MASK;
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
	if (pool->mode & PMALLOC_POOL_RW)
		tag_mask |= VM_PMALLOC_REWRITABLE;
	__tag_area(new_area, tag_mask);
	if (pool->mode & PMALLOC_POOL_START_PROTECTED) {
		__protect_area(new_area);
	} else if (pool->mode & PMALLOC_POOL_AUTO_PROTECT &&
		   !__empty(pool)) {
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
	    unlikely(grow(pool, size) != 0))
			goto error;
	pool->offset = round_down(pool->offset - size, pool->align);
	retval = (void *)(__current_area(pool)->va_start + pool->offset);
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
 * In case the pool was configured to automatically protect memory when
 * allocating it, the configuration is dropped.
 */
void pmalloc_make_pool_ro(struct pmalloc_pool *pool)
{
	struct vmap_area *area;

	mutex_lock(&pool->mutex);
	pool->mode &= ~(PMALLOC_POOL_RW | PMALLOC_POOL_START_PROTECTED);
	llist_for_each_entry(area, pool->vm_areas.first, area_list)
		__protect_area(area);
	mutex_unlock(&pool->mutex);
}
EXPORT_SYMBOL(pmalloc_make_pool_ro);

/**
 * pmalloc_is_address_protected() - checks if the corresponding page is RO
 * @p: the address to test
 *
 * Return: true is the page containing the address is RO, false otherwise
 *
 * In cases of concurrent threads, it might be necessary to introduce
 * locking with the thread responsible of protecting the data.
 */
bool pmalloc_is_address_protected(void *p)
{
	struct page *page;
	struct vm_struct *area;

	if (unlikely(!is_vmalloc_addr(p)))
		return false;
	page = vmalloc_to_page(p);
	if (unlikely(!page))
		return false;
	wmb();
	area = page->area;
	if (unlikely((!area) ||
		     ((area->flags & VM_PMALLOC_PROTECTED_MASK) !=
		      VM_PMALLOC_PROTECTED_MASK)))
		return false;
	return true;
}
EXPORT_SYMBOL(pmalloc_is_address_protected);

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
