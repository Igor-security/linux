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
	size_t refill;
	size_t offset;
	size_t align;
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

static inline void protect_area(struct vmap_area *area)
{
	if (unlikely(is_area_protected(area)))
		return;
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED;
}

static inline void destroy_area(struct vmap_area *area)
{
	WARN(!is_area_protected(area), "Destroying unprotected area.");
	area->vm->flags &= ~(VM_PMALLOC | VM_PMALLOC_PROTECTED);
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
	size_t space_before;
	size_t space_after;

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
struct pmalloc_pool *pmalloc_create_custom_pool(size_t refill,
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
EXPORT_SYMBOL(pmalloc_create_custom_pool);


static int grow(struct pmalloc_pool *pool, size_t min_size)
{
	void *addr;
	struct vmap_area *area;
	unsigned long size;

	size = (min_size > pool->refill) ? min_size : pool->refill;
	addr = vmalloc(size);
	if (WARN(!addr, "Failed to allocate %zd bytes", PAGE_ALIGN(size)))
		return -ENOMEM;

	area = find_vmap_area((unsigned long)addr);
	tag_area(area);
	pool->offset = area->vm->nr_pages * PAGE_SIZE;
	llist_add(&area->area_list, &pool->vm_areas);
	return 0;
}

static void *reserve_mem(struct pmalloc_pool *pool, size_t size)
{
	pool->offset = round_down(pool->offset - size, pool->align);
	return (void *)(current_area(pool)->va_start + pool->offset);

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
	if (unlikely(space_needed(pool, size)) &&
	    unlikely(grow(pool, size)))
			goto out;
	retval = reserve_mem(pool, size);
out:
	mutex_unlock(&pool->mutex);
	return retval;
}
EXPORT_SYMBOL(pmalloc);

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
		protect_area(area);
	mutex_unlock(&pool->mutex);
}
EXPORT_SYMBOL(pmalloc_protect_pool);


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
EXPORT_SYMBOL(pmalloc_destroy_pool);


int pippo(void);

int pippo(void)
{
	struct pmalloc_pool *pool;
	char *var1, *var2;
	char *alias;
	struct page *page_from_array;
	struct page *page_from_pointer;
	int i;
	struct vm_struct *vm_struct;
	struct vmap_area *vmap_area;
	void *phys;
	void *remapped_addr;

	pool = pmalloc_create_pool();
	var1 = pmalloc(pool, PAGE_SIZE);
	pr_info("pippo var1              = 0x%p", var1);

	vmap_area = find_vmap_area((unsigned long)var1);
	pr_info("pippo vmap_area         = 0x%p", vmap_area);

	vm_struct = vmap_area->vm;
	pr_info("pippo vm_struct         = 0x%p", vm_struct);

	page_from_array = vm_struct->pages[0];
	pr_info("pippo page_from_array   = 0x%p", page_from_array);

	page_from_pointer = vmalloc_to_page(var1);
	pr_info("pippo page_from_pointer = 0x%p", page_from_pointer);

	phys = (void *)page_to_phys(page_from_pointer);
	pr_info("pippo phys              = 0x%p", phys);

	*var1 = 25;
	pmalloc_protect_pool(pool);

	remapped_addr = vmap(&page_from_array, 1, VM_MAP, PAGE_KERNEL);
	pr_info("pippo remapped_addr = 0x%p", remapped_addr);

	var2 = (char*)remapped_addr;
	pr_info("pippo var2 = %d", (int)*var2);

	*var2 = 19;
	pr_info("pippo var2 = %d", (int)*var2);
	pr_info("pippo var1 = %d", (int)*var1);
	vunmap(remapped_addr);
//	*var2 = 1;
	pr_info("pippo var1 = %d", (int)*var1);
//	*var1 = 1;
	return 0;
}
core_initcall(pippo);
