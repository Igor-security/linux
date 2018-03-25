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
	unsigned short align;
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
	set_memory_rw(area->va_start, area->vm->nr_pages);
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
	return unlikely(pool->offset < round_up(size, pool->align));
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
 *               Negative values give the default of size_t.
 *
 * Creates a new (empty) memory pool for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
struct pmalloc_pool *pmalloc_create_custom_pool(unsigned long refill,
						short align_order)
{
	struct pmalloc_pool *pool;

	pool = kzalloc(sizeof(struct pmalloc_pool), GFP_KERNEL);
	if (WARN(!pool, "Could not allocate pool meta data."))
		return NULL;

	pool->refill = refill ? PAGE_ALIGN(refill) : DEFAULT_REFILL_SIZE;
	pool->align = 1 << (align_order < 0 ? sizeof(size_t) : align_order);
	mutex_init(&pool->mutex);

	mutex_lock(&pools_mutex);
	list_add(&pool->pool_node, &pools_list);
	mutex_unlock(&pools_mutex);
	return pool;
}

#define trace_grow() \
	pr_err("size: %zd    refill: %lu", size, pool->refill); \
	pr_err("Allocated addr: 0x%px", addr); \
	pr_err("va_start 0x%px", (void*)area->va_start); \
	pr_err("va_end   0x%px", (void*)area->va_end); \
	pr_err("nr_pages   %u", area->vm->nr_pages); \
	pr_err("size     %lu       %lu", \
	       area->vm->size, area->va_end - area->va_start); \
	pr_err("addr     0x%px", area->vm->addr);

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
	trace_grow();
	return 0;
}

static unsigned long reserve_mem(struct pmalloc_pool *pool, size_t size,
				 unsigned short align)
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
 * * NULL				- no memory available
 */
void *pmalloc(struct pmalloc_pool *pool, size_t size)
{
	unsigned long retval = 0;

	mutex_lock(&pool->mutex);
	if (space_needed(pool, size))
		if (unlikely(grow(pool, size)))
			goto out;
	retval = reserve_mem(pool, size, 0);
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
	struct llist_node *tmp;

	mutex_lock(&pools_mutex);
	list_del(&pool->pool_node);
	mutex_unlock(&pools_mutex);

	mutex_lock(&pool->mutex);
	while (pool->vm_areas.first) {
		tmp = pool->vm_areas.first;
		pool->vm_areas.first = pool->vm_areas.first->next;
		area = llist_entry(tmp, struct vmap_area, area_list);
		destroy_area(area);
	}
	mutex_unlock(&pool->mutex);
	kfree(pool);
}


static void pmalloc_test(void)
{
	struct pmalloc_pool *pool;
	void *p1, *p2, *p3;

	pool = pmalloc_create_pool();
	pr_err("XXXXXXXXXXXXXXXXXXXXXXXX pool: %px, refill: %ld",
	       pool, pool->refill / PAGE_SIZE);
	p1 = pmalloc(pool, sizeof(int));
	*(int *)p1 = -1;
	pr_err("1)  p1: 0x%px  *p1: %d", p1, *(int*)p1);
	p2 = pmalloc(pool, PAGE_SIZE - sizeof(int));
	pr_err("2)  p2: 0x%px  *p2: %d", p2, *(int*)p2);
	p3 = pmalloc(pool, sizeof(int));
	*(int *)p3 = 2;
	pr_err("3)  p3: 0x%px  *p3: %d", p3, *(int*)p3);
	pmalloc_protect_pool(pool);
//	*(int *)p3 = 5;
	pmalloc_destroy_pool(pool);
}

static int __init pmalloc_late_init(void)
{
	pmalloc_test();
	return 0;
}
late_initcall(pmalloc_late_init);
