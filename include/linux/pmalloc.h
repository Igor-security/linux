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


/*
 * Library for dynamic allocation of pools of memory that can be,
 * after initialization, marked as read-only.
 *
 * This is intended to complement __read_only_after_init, for those cases
 * where either it is not possible to know the initialization value before
 * init is completed, or the amount of data is variable and can be
 * determined only at run-time.
 */


struct pmalloc_pool *pmalloc_create_custom_pool(unsigned long int refill,
						short int align_order);

/**
 * pmalloc_create_pool() - create a new protectable memory pool
 *
 * Shorthand for pmalloc_create_custom_pool() with default arguments.
 *
 * Return:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
static inline struct pmalloc_pool *pmalloc_create_pool(void)
{
	return pmalloc_create_custom_pool(0, -1);
}

int is_pmalloc_object(const void *ptr, const unsigned long n);


//bool pmalloc_expand_pool(struct gen_pool *pool, size_t size);


void *pmalloc(struct pmalloc_pool *pool, size_t size);


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
static inline void *pzalloc(struct pmalloc_pool *pool, size_t size)
{
	void *ptr = pmalloc(pool, size);

	if (likely(ptr))
		memset(ptr, 0, size);

	return ptr;
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
static inline void *pmalloc_array(struct pmalloc_pool *pool, size_t n,
				  size_t size)
{
	return pmalloc(pool, n * size);
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
static inline void *pcalloc(struct pmalloc_pool *pool, size_t n,
			    size_t size)
{
	return pmalloc_array(pool, n, size);
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


void pmalloc_protect_pool(struct pmalloc_pool *pool);


void pmalloc_destroy_pool(struct pmalloc_pool *pool);

#endif
