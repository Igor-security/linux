/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmalloc.h: Header for Protectable Memory Allocator
 *
 * (C) Copyright 2017-18 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PMALLOC_H
#define _LINUX_PMALLOC_H


#include <linux/string.h>
#include <linux/slab.h>

/*
 * Library for dynamic allocation of pools of protectable memory.
 * A pool is a single linked list of vmap_area structures.
 * Whenever a pool is protected, all the areas it contains at that point
 * are write protected.
 * More areas can be added and protected, in the same way.
 * Memory in a pool cannot be individually unprotected, but the pool can
 * be destroyed.
 * Upon destruction of a certain pool, all the related memory is released,
 * including its metadata.
 *
 * Depending on the type of protection that was choosen, the memory can be
 * either completely read-only or it can support rare-writes.
 *
 * The rare-write mechanism is intended to provide no read overhead and
 * still some form of protection, while a selected area is modified.
 * This will incur into a penalty that is partially depending on the
 * specific architecture, but in general is the price to pay for limiting
 * the attack surface, while the change takes place.
 *
 * For additional safety, it is not possible to have in the same pool both
 * rare-write and unmodifiable memory.
 */


#define PMALLOC_REFILL_DEFAULT (0)
#define PMALLOC_ALIGN_DEFAULT ARCH_KMALLOC_MINALIGN
#define PMALLOC_RO 0
#define PMALLOC_RW 1

struct pmalloc_pool *pmalloc_create_custom_pool(size_t refill,
						bool rewritable,
						unsigned short align_order);

/**
 * pmalloc_create_pool() - create a protectable memory pool
 * @rewritable: can the data be altered after protection
 *
 * Shorthand for pmalloc_create_custom_pool() with default argument:
 * * refill is set to PMALLOC_REFILL_DEFAULT
 * * align_order is set to PMALLOC_ALIGN_DEFAULT
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
static inline struct pmalloc_pool *pmalloc_create_pool(bool rewritable)
{
	return pmalloc_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					  rewritable,
					  PMALLOC_ALIGN_DEFAULT);
}


void *pmalloc(struct pmalloc_pool *pool, size_t size);


/**
 * pzalloc() - zero-initialized version of pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @size: amount of memory (in bytes) requested
 *
 * Executes pmalloc(), initializing the memory requested to 0, before
 * returning its address.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- error
 */
static inline void *pzalloc(struct pmalloc_pool *pool, size_t size)
{
	void *ptr = pmalloc(pool, size);

	if (likely(ptr))
		memset(ptr, 0, size);
	return ptr;
}


/**
 * pmalloc_array() - array version of pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested for each element
 *
 * Executes pmalloc(), on an array.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */

static inline void *pmalloc_array(struct pmalloc_pool *pool, size_t n,
				  size_t size)
{
	if (unlikely(size != 0) && unlikely(n > SIZE_MAX / size))
		return NULL;
	return pmalloc(pool, n * size);
}


/**
 * pcalloc() - array version of pzalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested for each element
 *
 * Executes pzalloc(), on an array.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */
static inline void *pcalloc(struct pmalloc_pool *pool, size_t n,
			    size_t size)
{
	if (unlikely(size != 0) && unlikely(n > SIZE_MAX / size))
		return NULL;
	return pzalloc(pool, n * size);
}


/**
 * pstrdup() - duplicate a string, using pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @s: string to duplicate
 *
 * Generates a copy of the given string, allocating sufficient memory
 * from the given pmalloc pool.
 *
 * Return:
 * * pointer to the replica	- success
 * * NULL			- error
 */
static inline char *pstrdup(struct pmalloc_pool *pool, const char *s)
{
	size_t len;
	char *buf;

	len = strlen(s) + 1;
	buf = pmalloc(pool, len);
	if (likely(buf))
		strncpy(buf, s, len);
	return buf;
}

bool pmalloc_rare_write(struct pmalloc_pool *pool, const void *destination,
			const void *source, size_t n_bytes);

void pmalloc_protect_pool(struct pmalloc_pool *pool);

void pmalloc_make_pool_ro(struct pmalloc_pool *pool);

void pmalloc_destroy_pool(struct pmalloc_pool *pool);
#endif
