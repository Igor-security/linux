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
#include <linux/genalloc.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/rculist.h>
#include <linux/set_memory.h>
#include <linux/bug.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

#include <linux/pmalloc.h>
/*
 * pmalloc_data contains the data specific to a pmalloc pool,
 * in a format compatible with the design of gen_alloc.
 * Some of the fields are used for exposing the corresponding parameter
 * to userspace, through sysfs.
 */
struct pmalloc_data {
	struct gen_pool *pool;  /* Link back to the associated pool. */
	bool protected;     /* Status of the pool: RO or RW. */
	struct kobj_attribute attr_protected; /* Sysfs attribute. */
	struct kobj_attribute attr_avail;     /* Sysfs attribute. */
	struct kobj_attribute attr_size;      /* Sysfs attribute. */
	struct kobj_attribute attr_chunks;    /* Sysfs attribute. */
	struct kobject *pool_kobject;
	struct list_head node; /* list of pools */
};

static LIST_HEAD(pmalloc_list);
static bool sysfs_ready;
static DEFINE_MUTEX(pmalloc_mutex);
static struct kobject *pmalloc_kobject;


/**
 * pmalloc_pool_show_protected() - shows if a pool is write-protected
 * @dev: the associated kobject
 * @attr:A handle to the attribute object
 * @buf: the buffer where to write the value
 *
 * Return:
 * * the number of bytes written
 */
static ssize_t pmalloc_pool_show_protected(struct kobject *dev,
					   struct kobj_attribute *attr,
					   char *buf)
{
	struct pmalloc_data *data;

	data = container_of(attr, struct pmalloc_data, attr_protected);
	if (data->protected)
		return sprintf(buf, "protected\n");
	else
		return sprintf(buf, "unprotected\n");
}


/**
 * pmalloc_pool_show_avail() - shows cumulative available space in a pool
 * @dev: the associated kobject
 * @attr:A handle to the attribute object
 * @buf: the buffer where to write the value
 *
 * The value shown is only indicative, because it doesn't take in account
 * various factors, like allocation strategy, nor fragmentation, both
 * across multiple chunks and even within the same chunk.
 *
 * Return:
 * * the number of bytes written
 */
static ssize_t pmalloc_pool_show_avail(struct kobject *dev,
				       struct kobj_attribute *attr,
				       char *buf)
{
	struct pmalloc_data *data;

	data = container_of(attr, struct pmalloc_data, attr_avail);
	return sprintf(buf, "%lu\n",
		       (unsigned long)gen_pool_avail(data->pool));
}


/**
 * pmalloc_pool_show_size() - shows cumulative size of a pool
 * @dev: the associated kobject
 * @attr: handle to the attribute object
 * @buf: the buffer where to write the value
 *
 * Return:
 * * the number of bytes written
 */
static ssize_t pmalloc_pool_show_size(struct kobject *dev,
				      struct kobj_attribute *attr,
				      char *buf)
{
	struct pmalloc_data *data;

	data = container_of(attr, struct pmalloc_data, attr_size);
	return sprintf(buf, "%lu\n",
		       (unsigned long)gen_pool_size(data->pool));
}

/**
 * pool_chunk_number() - callback to count the number of chunks in a pool
 * @pool: handle to the pool
 * @chunk: chunk for the current iteration
 * @data: opaque data passed by the calling iterator
 */
static void pool_chunk_number(struct gen_pool *pool,
			      struct gen_pool_chunk *chunk, void *data)
{
	unsigned long *counter = data;

	(*counter)++;
}

/**
 * pmalloc_pool_show_chunks() - callback exposing the number of chunks
 * @dev: the associated kobject
 * @attr: handle to the attribute object
 * @buf: the buffer where to write the value
 *
 * Return:
 * * number of bytes written
 */
static ssize_t pmalloc_pool_show_chunks(struct kobject *dev,
					struct kobj_attribute *attr,
					char *buf)
{
	struct pmalloc_data *data;
	unsigned long chunks_num = 0;

	data = container_of(attr, struct pmalloc_data, attr_chunks);
	gen_pool_for_each_chunk(data->pool, pool_chunk_number, &chunks_num);
	return sprintf(buf, "%lu\n", chunks_num);
}


/**
 * pmalloc_connect() - Exposes the pool and its attributes through sysfs.
 * @data: pointer to the data structure describing a pool
 *
 * Return:
 * * pointer	- to the kobject created
 * * NULL	- error
 */
static struct kobject *pmalloc_connect(struct pmalloc_data *data)
{
	const struct attribute *attrs[] = {
		&data->attr_protected.attr,
		&data->attr_avail.attr,
		&data->attr_size.attr,
		&data->attr_chunks.attr,
		NULL
	};
	struct kobject *kobj;

	kobj = kobject_create_and_add(data->pool->name, pmalloc_kobject);
	if (unlikely(!kobj))
		return NULL;

	if (unlikely(sysfs_create_files(kobj, attrs) < 0)) {
		kobject_put(kobj);
		return NULL;
	}
	return kobj;
}

/**
 * pmalloc_disconnect() - Removes the pool and its attributes from sysfs.
 * @data: opaque data passed from the caller
 * @kobj: the object to disconnect
 */
static void pmalloc_disconnect(struct pmalloc_data *data,
			       struct kobject *kobj)
{
	const struct attribute *attrs[] = {
		&data->attr_protected.attr,
		&data->attr_avail.attr,
		&data->attr_size.attr,
		&data->attr_chunks.attr,
		NULL
	};

	sysfs_remove_files(kobj, attrs);
	kobject_put(kobj);
}

/* Declares an attribute of the pool. */
#define pmalloc_attr_init(data, attr_name) \
do { \
	sysfs_attr_init(&data->attr_##attr_name.attr); \
	data->attr_##attr_name.attr.name = #attr_name; \
	data->attr_##attr_name.attr.mode = VERIFY_OCTAL_PERMISSIONS(0400); \
	data->attr_##attr_name.show = pmalloc_pool_show_##attr_name; \
} while (0)


/**
 * init_pool() - allocates and initializes the data strutures for a pool
 * @pool: handle to the pool to initialise.
 * @name: the name for the new pool,
 *
 * Return:
 * * true	- success
 * * false	- failed allocations for meta-data
 */
static inline bool init_pool(struct gen_pool *pool, const char *name)
{
	const char *pool_name;
	struct pmalloc_data *data;

	pool_name = kstrdup(name, GFP_KERNEL);
	if (WARN(!pool_name, "failed to allocate memory for pool name"))
		return false;

	data = kzalloc(sizeof(struct pmalloc_data), GFP_KERNEL);
	if (WARN(!data, "failed to allocate memory for pool data")) {
		kfree(pool_name);
		return false;
	}

	data->protected = false;
	data->pool = pool;
	pmalloc_attr_init(data, protected);
	pmalloc_attr_init(data, avail);
	pmalloc_attr_init(data, size);
	pmalloc_attr_init(data, chunks);
	pool->data = data;
	pool->name = pool_name;
	return true;
}


/**
 * pmalloc_create_pool() - create a new protectable memory pool
 * @name: the name of the pool, enforced to be unique
 * @min_alloc_order: log2 of the minimum allocation size obtainable
 *                   from the pool; -1 will pick sizeof(unsigned long)
 *
 * Creates a new (empty) memory pool for allocation of protectable
 * memory. Memory will be allocated upon request (through pmalloc).
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
struct gen_pool *pmalloc_create_pool(const char *name, int min_alloc_order)
{
	struct gen_pool *pool;
	struct pmalloc_data *data;

	if (WARN(!name, "Refusing to create unnamed pool"))
		return NULL;

	if (min_alloc_order < 0)
		min_alloc_order = ilog2(sizeof(unsigned long));

	pool = gen_pool_create(min_alloc_order, NUMA_NO_NODE);
	if (WARN(!pool, "Could not allocate memory for pool"))
		return NULL;

	if (WARN(!init_pool(pool, name),
		 "Failed to initialize pool %s.", name))
		goto init_pool_err;

	mutex_lock(&pmalloc_mutex);
	list_for_each_entry(data, &pmalloc_list, node) {
		if (!strcmp(name, data->pool->name)) {
			mutex_unlock(&pmalloc_mutex);
			goto same_name_err;
		}
	}

	data = (struct pmalloc_data *)pool->data;
	list_add(&data->node, &pmalloc_list);
	if (sysfs_ready)
		data->pool_kobject = pmalloc_connect(data);
	mutex_unlock(&pmalloc_mutex);
	return pool;

same_name_err:
	kfree(pool->data);
init_pool_err:
	gen_pool_destroy(pool);
	return NULL;
}

#define CHUNK_TAG true
#define CHUNK_UNTAG false
/**
 * chunk_tagging() - (un)tags the area corresponding to a chunk
 * @chunk: vmalloc allocation, as multiple of memory pages
 * @tag: selects whether to tag or untag the pages from the chunk
 *
 * Return:
 * * true	- success
 * * false	- failure
 */
static inline bool chunk_tagging(void *chunk, bool tag)
{
	struct vm_struct *area;
	struct page *page;

	if (!is_vmalloc_addr(chunk))
		return false;

	page = vmalloc_to_page(chunk);
	if (unlikely(!page))
		return false;

	area = page->area;
	if (tag == CHUNK_UNTAG)
		area->flags &= ~VM_PMALLOC;
	else
		area->flags |= VM_PMALLOC;
	return true;
}


enum {
	INVALID_PMALLOC_OBJECT = -1,
	NOT_PMALLOC_OBJECT = 0,
	VALID_PMALLOC_OBJECT = 1,
};


/**
 * is_pmalloc_object() - validates the existence of an alleged object
 * @ptr: address of the object
 * @n: size of the object, in bytes
 *
 * Return:
 * * 0		- the object does not belong to pmalloc
 * * 1		- the object belongs to pmalloc
 * * \-1	- the object overlaps pmalloc memory incorrectly
 */
int is_pmalloc_object(const void *ptr, const unsigned long n)
{
	struct vm_struct *area;
	struct page *page;
	unsigned long area_start;
	unsigned long area_end;
	unsigned long object_start;
	unsigned long object_end;


	/*
	 * is_pmalloc_object gets called pretty late, so chances are high
	 * that the object is indeed of vmalloc type
	 */
	if (unlikely(!is_vmalloc_addr(ptr)))
		return NOT_PMALLOC_OBJECT;

	page = vmalloc_to_page(ptr);
	if (unlikely(!page))
		return NOT_PMALLOC_OBJECT;

	area = page->area;

	if (likely(!(area->flags & VM_PMALLOC)))
		return NOT_PMALLOC_OBJECT;

	area_start = (unsigned long)area->addr;
	area_end = area_start + area->nr_pages * PAGE_SIZE - 1;
	object_start = (unsigned long)ptr;
	object_end = object_start + n - 1;

	if (likely((area_start <= object_start) &&
		   (object_end <= area_end)))
		return VALID_PMALLOC_OBJECT;
	else
		return INVALID_PMALLOC_OBJECT;
}


/**
 * pmalloc_expand_pool() - adds a memory chunk of the requested size
 * @pool: handle for the pool
 * @size: amount of memory (in bytes) requested
 *
 * Prepares a chunk of the requested size.
 * This is intended to both minimize latency in later memory requests and
 * avoid sleeping during allocation.
 * Memory allocated with prealloc is stored in one single chunk, as
 * opposed to what is allocated on-demand when pmalloc runs out of free
 * space already existing in the pool and has to invoke vmalloc.
 * One additional advantage of pre-allocating larger chunks of memory is
 * that the total slack tends to be smaller.
 * If used for avoiding sleep, the intended user must be protected from
 * other, parasitic users, for example with a lock.
 *
 * Return:
 * * true	- allocation and registration were successful
 * * false	- some error occurred
 */
bool pmalloc_expand_pool(struct gen_pool *pool, size_t size)
{
	void *chunk;
	size_t chunk_size;

	chunk_size = roundup(size, PAGE_SIZE);
	chunk = vmalloc(chunk_size);
	if (WARN(chunk == NULL,
		 "Could not allocate %zu bytes from vmalloc", chunk_size))
		return false;

	if (WARN(!chunk_tagging(chunk, CHUNK_TAG),
		 "Failed to tag chunk as pmalloc memory"))
		goto free;

	/* Locking is already done inside gen_pool_add */
	if (WARN(gen_pool_add(pool, (unsigned long)chunk, chunk_size,
			      NUMA_NO_NODE),
		 "Failed to add chunk to pool %s", pool->name)) {
		chunk_tagging(chunk, CHUNK_UNTAG);
free:
		/*
		 * expand_pool might be called with a lock held, so use
		 * vfree_atomic, instaed of plain vfree.
		 */
		vfree_atomic(chunk);
		return false;
	}

	return true;

}


/**
 * pmalloc() - allocate protectable memory from a pool
 * @pool: handle to the pool to be used for memory allocation
 * @size: amount of memory (in bytes) requested
 * @gfp: flags for page allocation
 *
 * Allocates memory from an unprotected pool. If the pool doesn't have
 * enough memory, and the request did not include GFP_ATOMIC, an attempt
 * is made to add a new chunk of memory to the pool
 * (a multiple of PAGE_SIZE), in order to fit the new request.
 * Otherwise, NULL is returned.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- either no memory available or
 *					  pool already read-only
 */
void *pmalloc(struct gen_pool *pool, size_t size, gfp_t gfp)
{
	unsigned long addr;
	struct pmalloc_data *data = (struct pmalloc_data *)(pool->data);

	if (WARN(data->protected, "pool %s already protected",
		 pool->name))
		return NULL;

	/*
	 * Even when everything goes fine, 2 or more allocations might
	 * happen in parallel, where one "steals" the memory added by
	 * another, but that's ok, just try to allocate some more.
	 * Eventually the "stealing" will subside.
	 */
	while (true) {
		/* Try to add enough memory to the pool. */
		addr = gen_pool_alloc(pool, size);
		if (likely(addr))
			break; /* Success! Retry the allocation. */

		/* There was no suitable memory available in the pool. */
		if (likely(!(gfp & __GFP_ATOMIC))) {
			/* Not in atomic context, expand the pool. */
			if (likely(pmalloc_expand_pool(pool, size)) ||
			    unlikely(gfp & __GFP_NOFAIL))
			/* Retry, either upon success or if mandated. */
				continue;
			/* Otherwise, give up. */
			WARN(true, "Could not add %zu bytes to %s pool",
			     size, pool->name);
			return NULL;
		}

		/* Atomic context: no chance to expand the pool. */
		if (WARN(!(gfp & __GFP_NOFAIL),
			 "Could not get %zu bytes from %s and ATOMIC",
			 size, pool->name))
			return NULL; /* Fail, if possible. */
		/* Otherwise, retry.*/
	}

	if (unlikely(gfp & __GFP_ZERO))
		memset((void *)addr, 0, size);
	return (void *)addr;
}


/**
 * pmalloc_chunk_set_protection() - (un)protects a pool
 * @pool: handle to the pool to (un)protect
 * @chunk: handle to the chunk to (un)protect
 * @data: opaque data from the chunk iterator - it's a boolean
 * * TRUE	- protect the chunk
 * * FALSE	- unprotect the chunk
 */
static void pmalloc_chunk_set_protection(struct gen_pool *pool,
					 struct gen_pool_chunk *chunk,
					 void *data)
{
	const bool *flag = data;
	size_t chunk_size = chunk->end_addr + 1 - chunk->start_addr;
	unsigned long pages = chunk_size / PAGE_SIZE;

	if (WARN(chunk_size & (PAGE_SIZE - 1),
		 "Chunk size is not a multiple of PAGE_SIZE."))
		return;

	if (*flag)
		set_memory_ro(chunk->start_addr, pages);
	else
		set_memory_rw(chunk->start_addr, pages);
}


/**
 * pmalloc_pool_set_protection() - (un)protects a pool
 * @pool: handle to the pool to (un)protect
 * @protection:
 * * TRUE	- protect
 * * FALSE	- unprotect
 */
static void pmalloc_pool_set_protection(struct gen_pool *pool,
					bool protection)
{
	struct pmalloc_data *data;
	struct gen_pool_chunk *chunk;

	data = pool->data;
	if (WARN(data->protected == protection,
		 "The pool %s is already protected as requested",
		 pool->name))
		return;
	data->protected = protection;
	list_for_each_entry(chunk, &(pool)->chunks, next_chunk)
		pmalloc_chunk_set_protection(pool, chunk, &protection);
}


/**
 * pmalloc_protect_pool() - turn a read/write pool into read-only
 * @pool: the pool to protect
 *
 * Write-protects all the memory chunks assigned to the pool.
 * This prevents any further allocation.
 */
void  pmalloc_protect_pool(struct gen_pool *pool)
{
	pmalloc_pool_set_protection(pool, true);
}


/**
 * pmalloc_chunk_free() - untags and frees the pages from a chunk
 * @pool: handle to the pool containing the chunk
 * @chunk: the chunk to free
 * @data: opaque data passed by the iterator invoking this function
 */
static void pmalloc_chunk_free(struct gen_pool *pool,
			       struct gen_pool_chunk *chunk, void *data)
{
	chunk_tagging(chunk, CHUNK_UNTAG);
	gen_pool_flush_chunk(pool, chunk);
	vfree_atomic((void *)chunk->start_addr);
}


/**
 * pmalloc_destroy_pool() - destroys a pool and all the associated memory
 * @pool: the pool to destroy
 *
 * All the memory that was allocated through pmalloc in the pool will be freed.
 */
void pmalloc_destroy_pool(struct gen_pool *pool)
{
	struct pmalloc_data *data;

	data = pool->data;

	mutex_lock(&pmalloc_mutex);
	list_del(&data->node);
	mutex_unlock(&pmalloc_mutex);

	if (likely(data->pool_kobject))
		pmalloc_disconnect(data, data->pool_kobject);

	pmalloc_pool_set_protection(pool, false);
	gen_pool_for_each_chunk(pool, pmalloc_chunk_free, NULL);
	gen_pool_destroy(pool);
	kfree(data);
}


/**
 * pmalloc_late_init() - registers to debug sysfs pools pretading it
 *
 * When the sysfs infrastructure is ready to receive registrations,
 * connect all the pools previously created. Also enable further pools
 * to be connected right away.
 *
 * Return:
 * * 0		- success
 * * \-1	- error
 */
static int __init pmalloc_late_init(void)
{
	struct pmalloc_data *data, *n;

	pmalloc_kobject = kobject_create_and_add("pmalloc", kernel_kobj);
	if (WARN(!pmalloc_kobject,
		 "Failed to create pmalloc root sysfs dir"))
		return -1;

	mutex_lock(&pmalloc_mutex);
	sysfs_ready = true;
	list_for_each_entry_safe(data, n, &pmalloc_list, node)
		pmalloc_connect(data);
	mutex_unlock(&pmalloc_mutex);

	return 0;
}
late_initcall(pmalloc_late_init);
