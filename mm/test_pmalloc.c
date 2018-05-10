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
#include <linux/prot_list.h>

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

#define REGION_SIZE (PAGE_SIZE / 4)
#define REGION_NUMBERS 12
static inline void fill_region(char *addr, char c)
{
	size_t i;

	for (i = 0; i < REGION_SIZE - 1; i++)
		addr[i] = c;
	addr[i] = '\0';
}

static inline void init_regions(char *array)
{
	size_t i;

	for (i = 0; i < REGION_NUMBERS; i++)
		fill_region(array + REGION_SIZE * i, i + 'A');
}

static inline void show_regions(char *array)
{
	size_t i;

	for (i = 0; i < REGION_NUMBERS; i++)
		pr_info("%s", array + REGION_SIZE * i);
}

static inline void init_big_injection(char *big_injection)
{
	size_t i;

	for (i = 0; i < PAGE_SIZE * 3; i++)
		big_injection[i] = 'X';
}

/* Verify rewritable feature. */
static int test_rare_write(void)
{
	struct pmalloc_pool *pool;
	char *array;
	char injection[] = "123456789";
	unsigned short size = sizeof(injection);
	char *big_injection;


	pr_notice("Test pmalloc_rare_write()");
	pool = pmalloc_create_pool(PMALLOC_RW);
	array = pzalloc(pool, REGION_SIZE * REGION_NUMBERS);
	init_regions(array);
	pmalloc_protect_pool(pool);
	pr_info("------------------------------------------------------");
	pmalloc_rare_write(pool, array, injection, size);
	pmalloc_rare_write(pool, array + REGION_SIZE, injection, size);
	pmalloc_rare_write(pool,
			   array + 5 * REGION_SIZE / 2 - size / 2,
			   injection, size);
	pmalloc_rare_write(pool, array + 3 * REGION_SIZE - size / 2,
			   injection, size);
	show_regions(array);
	pmalloc_destroy_pool(pool);
	pr_info("------------------------------------------------------");
	pool = pmalloc_create_pool(PMALLOC_RW);
	array = pzalloc(pool, REGION_SIZE * REGION_NUMBERS);
	init_regions(array);
	pmalloc_protect_pool(pool);
	big_injection = vmalloc(PAGE_SIZE * 3);
	init_big_injection(big_injection);
	pmalloc_rare_write(pool, array + REGION_SIZE / 2, big_injection,
			   PAGE_SIZE * 2);
	show_regions(array);
	pr_info("------------------------------------------------------");
	return 0;
}

struct test_data {
	int data_int;
	struct prot_head list;
	unsigned long long data_ulong;
};

static int test_prot_list(void)
{
	struct prot_list_pool *pool;
	struct prot_head *head;
	struct prot_head *cursor;
	struct test_data data;
	int i;

	/* Create a pool for the protectable list. */
	pool = prot_list_create_pool();
	if (WARN(!pool, "could not create pool"))
		return -ENOMEM;

	head = PROT_LIST_HEAD(pool);
	for (i = 0; i < 100; i++) {
		data.data_int = i;
		data.data_ulong = i * i;
		if (i % 2)
			prot_list_append(pool, head, &data, list);
		else
			prot_list_prepend(pool, head, &data, list);
	}
	for (cursor = head->next; cursor != head; cursor = cursor->next) {
		struct test_data *data;

		data = container_of(cursor, struct test_data, list);

		pr_info("cursor: 0x%08lx  data_int: %02d ",
			(unsigned long)cursor, data->data_int);
	}
/*	{
		struct test_data *data;

		data = container_of(cursor->next, struct test_data, list);
		data->data_int += 5;
	}*/
	for (cursor = head->prev; cursor != head; cursor = cursor->prev) {
		struct test_data *data;

		data = container_of(cursor, struct test_data, list);

		pr_info("cursor: 0x%08lx  data_int: %02d ",
			(unsigned long)cursor, data->data_int);
	}

	return 0;
}

/**
 * test_pmalloc()  -main entry point for running the test cases
 */

static int __init test_pmalloc_init_module(void)
{
	pr_notice("pmalloc-selftest");

	if (unlikely(!(create_and_destroy_pool() &&
		       test_alloc() &&
		       test_is_pmalloc_object())))
		return -1;
	test_rare_write();
	test_prot_list();
	return 0;
}

module_init(test_pmalloc_init_module);

static void __exit test_pmalloc_cleanup_module(void)
{
}

module_exit(test_pmalloc_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for pmalloc.");
