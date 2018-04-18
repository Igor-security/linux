// SPDX-License-Identifier: GPL-2.0
/*
 * test_pmalloc.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/pmalloc.h>
#include <linux/mm.h>
#include <linux/test_pmalloc.h>
#include <linux/bug.h>

#include "pmalloc_helpers.h"

#define SIZE_1 (PAGE_SIZE * 3)
#define SIZE_2 1000

/* wrapper for is_pmalloc_object() with messages */
static inline bool validate_alloc(bool expected, void *addr,
				  unsigned long size)
{
	bool test;

	test = is_pmalloc_object(addr, size) > 0;
	pr_notice("must be %s: %s",
		  expected ? "ok" : "no", test ? "ok" : "no");
	return test == expected;
}


#define is_alloc_ok(variable, size)	\
	validate_alloc(true, variable, size)


#define is_alloc_no(variable, size)	\
	validate_alloc(false, variable, size)

/* tests the basic life-cycle of a pool */
static bool create_and_destroy_pool(void)
{
	static struct pmalloc_pool *pool;

	pr_notice("Testing pool creation and destruction capability");

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, "Cannot allocate memory for pmalloc selftest."))
		return false;
	pmalloc_destroy_pool(pool);
	return true;
}


/*  verifies that it's possible to allocate from the pool */
static bool test_alloc(void)
{
	static struct pmalloc_pool *pool;
	static void *p;

	pr_notice("Testing allocation capability");
	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, "Unable to allocate memory for pmalloc selftest."))
		return false;
	p = pmalloc(pool,  SIZE_1 - 1);
	pmalloc_protect_pool(pool);
	pmalloc_destroy_pool(pool);
	if (WARN(!p, "Failed to allocate memory from the pool"))
		return false;
	return true;
}


/* tests the identification of pmalloc ranges */
static bool test_is_pmalloc_object(void)
{
	struct pmalloc_pool *pool;
	void *pmalloc_p;
	void *vmalloc_p;
	bool retval = false;

	pr_notice("Test correctness of is_pmalloc_object()");

	vmalloc_p = vmalloc(SIZE_1);
	if (WARN(!vmalloc_p,
		 "Unable to allocate memory for pmalloc selftest."))
		return false;
	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, "Unable to allocate memory for pmalloc selftest."))
		return false;
	pmalloc_p = pmalloc(pool,  SIZE_1 - 1);
	if (WARN(!pmalloc_p, "Failed to allocate memory from the pool"))
		goto error;
	if (WARN_ON(unlikely(!is_alloc_ok(pmalloc_p, 10))) ||
	    WARN_ON(unlikely(!is_alloc_ok(pmalloc_p, SIZE_1))) ||
	    WARN_ON(unlikely(!is_alloc_ok(pmalloc_p, PAGE_SIZE))) ||
	    WARN_ON(unlikely(!is_alloc_no(pmalloc_p, SIZE_1 + 1))) ||
	    WARN_ON(unlikely(!is_alloc_no(vmalloc_p, 10))))
		goto error;
	retval = true;
error:
	pmalloc_protect_pool(pool);
	pmalloc_destroy_pool(pool);
	vfree(vmalloc_p);
	return retval;
}

/* Test out of virtually contiguous memory */
static void test_oovm(void)
{
	struct pmalloc_pool *pool;
	unsigned int i;

	pr_notice("Exhaust vmalloc memory with doubling allocations.");
	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, "Failed to create pool"))
		return;
	for (i = 1; i; i *= 2)
		if (unlikely(!pzalloc(pool, i - 1)))
			break;
	pr_notice("vmalloc oom at %d allocation", i - 1);
	pmalloc_protect_pool(pool);
	pmalloc_destroy_pool(pool);
}

/**
 * test_pmalloc()  -main entry point for running the test cases
 */
void test_pmalloc(void)
{

	pr_notice("pmalloc-selftest");

	if (unlikely(!(create_and_destroy_pool() &&
		       test_alloc() &&
		       test_is_pmalloc_object())))
		return;
	test_oovm();
}
