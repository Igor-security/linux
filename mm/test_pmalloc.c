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

#define SIZE_1 (PAGE_SIZE * 3)
#define SIZE_2 1000

static struct gen_pool *pool_unprot;
static struct gen_pool *pool_prot;
static struct gen_pool *pool_pre;

static void *var_prot;
static void *var_unprot;
static void *var_vmall;

/**
 * validate_alloc() - wrapper for is_pmalloc_object with messages
 * @expected: whether if the test is supposed to be ok or not
 * @addr: base address of the range to test
 * @size: length of he range to test
 */
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

/**
 * create_pools() - tries to instantiate the pools needed for the test
 *
 * Creates the respective instances for each pool used in the test.
 * In case of error, it rolls back whatever previous step passed
 * successfully.
 *
 * Return:
 * * true	- success
 * * false	- something failed
 */
static bool create_pools(void)
{
	pr_notice("Testing pool creation capability");

	pool_pre = pmalloc_create_pool("preallocated", 0);
	if (unlikely(!pool_pre))
		goto err_pre;

	pool_unprot = pmalloc_create_pool("unprotected", 0);
	if (unlikely(!pool_unprot))
		goto err_unprot;

	pool_prot = pmalloc_create_pool("protected", 0);
	if (unlikely(!(pool_prot)))
		goto err_prot;
	return true;
err_prot:
	pmalloc_destroy_pool(pool_unprot);
err_unprot:
	pmalloc_destroy_pool(pool_pre);
err_pre:
	WARN(true, "Unable to allocate memory for pmalloc selftest.");
	return false;
}


/**
 * destroy_pools() - tears down the instances of the pools in use
 *
 * Mostly used on the path for error recovery, when something goes wrong,
 * the pools allocated are dropped.
 */
static void destroy_pools(void)
{
	pmalloc_destroy_pool(pool_prot);
	pmalloc_destroy_pool(pool_unprot);
	pmalloc_destroy_pool(pool_pre);
}


/**
 * test_alloc() - verifies that it's possible to allocate from the pools
 *
 * Each of the pools declared must be available for allocation, at this
 * point. There is also a small allocation from generic vmallco memory.
 */
static bool test_alloc(void)
{
	pr_notice("Testing allocation capability");

	var_vmall = vmalloc(SIZE_2);
	if (unlikely(!var_vmall))
		goto err_vmall;

	var_unprot = pmalloc(pool_unprot,  SIZE_1 - 1, GFP_KERNEL);
	if (unlikely(!var_unprot))
		goto err_unprot;

	var_prot = pmalloc(pool_prot,  SIZE_1, GFP_KERNEL);
	if (unlikely(!var_prot))
		goto err_prot;

	return true;
err_prot:
	pfree(pool_unprot, var_unprot);
err_unprot:
	vfree(var_vmall);
err_vmall:
	WARN(true, "Unable to allocate memory for pmalloc selftest.");
	return false;
}


/**
 * test_is_pmalloc_object() - tests the identification of pmalloc ranges
 *
 * Positive and negative test of potential pmalloc objects.
 *
 * Return:
 * * true	- success
 * * false	- error
 */
static bool test_is_pmalloc_object(void)
{
	pr_notice("Test correctness of is_pmalloc_object()");
	if (WARN_ON(unlikely(!is_alloc_ok(var_unprot, 10))) ||
	    WARN_ON(unlikely(!is_alloc_ok(var_unprot, SIZE_1))) ||
	    WARN_ON(unlikely(!is_alloc_ok(var_unprot, PAGE_SIZE))) ||
	    WARN_ON(unlikely(!is_alloc_no(var_unprot, SIZE_1 + 1))) ||
	    WARN_ON(unlikely(!is_alloc_no(var_vmall, 10))))
		return false;
	return true;
}


/**
 * test_protected_allocation() - allocation from protected pool must fail
 *
 * Once the pool is protected, the pages associated with it become
 * read-only and any further attempt to allocate data will be declined.
 *
 * Return:
 * * true	- success
 * * false	- error
 */
static bool test_protected_allocation(void)
{
	pmalloc_protect_pool(pool_prot);
	/*
	 * This will intentionally trigger a WARN, because the pool being
	 * allocated from is already protected.
	 */
	pr_notice("Test allocation from a protected pool. It will WARN.");
	return !WARN(unlikely(pmalloc(pool_prot, 10, GFP_KERNEL)),
		     "no memory from a protected pool");
}


/**
 * test_destroy_pool() - destroying an unprotected pool must WARN
 *
 * Attempting to destroy an unprotected pool will issue a warning, while
 * destroying a protected pool is considered to be the normal behavior.
 */
static void test_destroy_pools(void)
{
	/*
	 * This will intentionally trigger a WARN because the pool being
	 * destroyed is not protected, which is unusual and should happen
	 * on error paths only, where probably other warnings are already
	 * displayed.
	 */
	pr_notice("pmalloc-selftest: WARN in pmalloc_pool_set_protection.");
	pmalloc_destroy_pool(pool_unprot);
	pr_notice("pmalloc-selftest: point for expected WARN passed.");

	/* This must not cause WARNings */
	pr_notice("pmalloc-selftest: Expect no WARN below.");
	pmalloc_destroy_pool(pool_prot);
	pr_notice("pmalloc-selftest: passed point for unexpected WARN.");
}


/**
 * test_pmalloc() - main entry point for running the test cases
 *
 * Performs various tests, each step subordinate to the successful
 * execution of the previous.
 */
void test_pmalloc(void)
{

	pr_notice("pmalloc-selftest");

	if (unlikely(!create_pools()))
		return;

	if (unlikely(!test_alloc()))
		goto err_alloc;


	if (unlikely(!test_is_pmalloc_object()))
		goto err_is_object;

	*(int *)var_prot = 0;
	pfree(pool_unprot, var_unprot);
	vfree(var_vmall);

	if (unlikely(!test_protected_allocation()))
		goto err_prot_all;

	test_destroy_pools();
	return;
err_prot_all:
err_is_object:
err_alloc:
	destroy_pools();
}
