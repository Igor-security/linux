// SPDX-License-Identifier: GPL-2.0
/*
 * test_pmalloc.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/pmalloc.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define SIZE_1 (PAGE_SIZE * 3)
#define SIZE_2 1000

/* wrapper for is_pmalloc_object() with messages */
static inline bool validate_alloc(bool expected, void *addr,
				  unsigned long size)
{
	bool test;

	test = (is_pmalloc_object(addr, size) == GOOD_PMALLOC_OBJECT);
	pr_notice("must be %s: %s",
		  expected ? "ok" : "no", test  ? "ok" : "no");
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
	pmalloc_protect_pool(pool); /* Protect to avoid WARNs on destroy */
	pmalloc_destroy_pool(pool);
	vfree(vmalloc_p);
	return retval;
}

/* Confirm that RO pools cannot be altered by rare write. */
static bool test_illegal_rare_write_ro_pool(void)
{
	struct pmalloc_pool *pool;
	int *var_ptr;
	bool retval = false;

	pr_notice("Test pmalloc illegal rare_write - it should WARN");
	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, "Failed to create pool"))
		return false;
	var_ptr = pmalloc(pool, sizeof(int));
	if (WARN(!var_ptr, "Failed to allocate memory from pool"))
		goto destroy_pool;
	*var_ptr = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(pmalloc_rare_write_int(pool, var_ptr, 0x5A),
		 "Unexpected successful write to R/O protected pool"))
		goto destroy_pool;
	retval = true;
	pr_notice("Test pmalloc illegal rare_write successful");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static int rare_write_data __rare_write_after_init = 0xA5;

static bool test_illegal_rare_write_static_rare_write_mem(void)
{
//	struct pmalloc_pool *pool;


//	return pmalloc_
	return true;
}

static bool test_illegal_rare_writes(void)
{
	return test_illegal_rare_write_static_rare_write_mem() &&
		test_illegal_rare_write_ro_pool();
}

#define INSERT_OFFSET (PAGE_SIZE * 3 / 2)
#define INSERT_SIZE (PAGE_SIZE * 2)
#define REGION_SIZE (PAGE_SIZE * 5)
/* Verify rare writes across multiple pages, unaligned to PAGE_SIZE. */
static bool test_pmalloc_rare_write_array(void)
{
	struct pmalloc_pool *pool;
	char *region;
	char *mod;
	unsigned int i;
	int retval = false;

	pr_notice("Test pmalloc_rare_write");
	pool = pmalloc_create_pool(PMALLOC_RW);
	if (WARN(!pool, "Failed to create pool"))
		return false;
	region = pzalloc(pool, REGION_SIZE);
	pmalloc_protect_pool(pool);
	if (WARN(!region, "Failed to allocate memory from pool"))
		goto destroy_pool;
	mod = vmalloc(INSERT_SIZE);
	if (WARN(!mod, "Failed to allocate memory from vmalloc"))
		goto destroy_pool;
	memset(mod, 0xA5, INSERT_SIZE);
	retval = !pmalloc_rare_write_array(pool, region + INSERT_OFFSET,
					   mod, INSERT_SIZE);
	if (WARN(retval, "rare_write_array failed"))
		goto free_mod;

	for (i = 0; i < REGION_SIZE; i++)
		if (INSERT_OFFSET <= i &&
		    i < (INSERT_SIZE + INSERT_OFFSET)) {
			if (WARN(region[i] != (char)0xA5,
				 "Failed to alter target area"))
				goto free_mod;
		} else {
			if (WARN(region[i] != 0,
				 "Unexpected alteration outside ragion"))
				goto free_mod;
		}
	retval = true;
	pr_notice("Test pmalloc_rare_write success");
free_mod:
	vfree(mod);
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

/**
 * test_pmalloc()  -main entry point for running the test cases
 */
static int __init test_pmalloc_init_module(void)
{
	pr_notice("pmalloc-selftest");

	if (unlikely(!(create_and_destroy_pool() &&
		       test_alloc() &&
		       test_is_pmalloc_object() &&
		       test_pmalloc_rare_write_array() &&
		       test_illegal_rare_writes())))
		return -1;
	return 0;
}

module_init(test_pmalloc_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for pmalloc.");
