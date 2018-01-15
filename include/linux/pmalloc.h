/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmalloc.h: Header for Protectable Memory Allocator
 *
 * (C) Copyright 2017 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PMALLOC_H
#define _LINUX_PMALLOC_H


#include <linux/genalloc.h>
#include <linux/string.h>

#define PMALLOC_DEFAULT_ALLOC_ORDER (-1)

/*
 * Library for dynamic allocation of pools of memory that can be,
 * after initialization, marked as read-only.
 *
 * This is intended to complement __read_only_after_init, for those cases
 * where either it is not possible to know the initialization value before
 * init is completed, or the amount of data is variable and can be
 * determined only at run-time.
 *
 * ***WARNING***
 * The user of the API is expected to synchronize:
 * 1) allocation,
 * 2) writes to the allocated memory,
 * 3) write protection of the pool,
 * 4) freeing of the allocated memory, and
 * 5) destruction of the pool.
 *
 * For a non-threaded scenario, this type of locking is not even required.
 *
 * Even if the library were to provide support for locking, point 2)
 * would still depend on the user taking the lock.
 */


struct gen_pool *pmalloc_create_pool(const char *name,
					 int min_alloc_order);


int is_pmalloc_object(const void *ptr, const unsigned long n);


bool pmalloc_expand_pool(struct gen_pool *pool, size_t size);


void *pmalloc(struct gen_pool *pool, size_t size, gfp_t gfp);


/**
 * pzalloc() - zero-initialized version of pmalloc
 * @pool: handle to the pool to be used for memory allocation
 * @size: amount of memory (in bytes) requested
 * @gfp: flags for page allocation
 *
 * Executes pmalloc, initializing the memory requested to 0,
 * before returning the pointer to it.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- either no memory available or
 *					  pool already read-only
 */
static inline void *pzalloc(struct gen_pool *pool, size_t size, gfp_t gfp)
{
	return pmalloc(pool, size, gfp | __GFP_ZERO);
}


/**
 * pmalloc_array() - allocates an array according to the parameters
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested for each element
 * @flags: flags for page allocation
 *
 * Executes pmalloc, if it has a chance to succeed.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */
static inline void *pmalloc_array(struct gen_pool *pool, size_t n,
				  size_t size, gfp_t flags)
{
	if (unlikely(!(pool && n && size)))
		return NULL;
	return pmalloc(pool, n * size, flags);
}


/**
 * pcalloc() - allocates a 0-initialized array according to the parameters
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested
 * @flags: flags for page allocation
 *
 * Executes pmalloc_array, if it has a chance to succeed.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */
static inline void *pcalloc(struct gen_pool *pool, size_t n,
			    size_t size, gfp_t flags)
{
	return pmalloc_array(pool, n, size, flags | __GFP_ZERO);
}


/**
 * pstrdup() - duplicate a string, using pmalloc as allocator
 * @pool: handle to the pool to be used for memory allocation
 * @s: string to duplicate
 * @gfp: flags for page allocation
 *
 * Generates a copy of the given string, allocating sufficient memory
 * from the given pmalloc pool.
 *
 * Return:
 * * pointer to the replica	- success
 * * NULL			- error
 */
static inline char *pstrdup(struct gen_pool *pool, const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (unlikely(pool == NULL || s == NULL))
		return NULL;

	len = strlen(s) + 1;
	buf = pmalloc(pool, len, gfp);
	if (likely(buf))
		strncpy(buf, s, len);
	return buf;
}


void pmalloc_protect_pool(struct gen_pool *pool);


/**
 * pfree() - frees memory previously allocated from a pool
 * @pool: handle to the pool used to allocate the memory to free
 * @addr: the beginning of the location to free
 *
 */
static inline void pfree(struct gen_pool *pool, const void *addr)
{
	gen_pool_free(pool, (unsigned long)addr, 0);
}


void pmalloc_destroy_pool(struct gen_pool *pool);

#endif
