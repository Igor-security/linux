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

static const char MSG_NO_POOL[] = "Cannot allocate memory for selftest.";
static const char MSG_NO_PMEM[] = "Cannot allocate memory from the pool.";

#define pr_success(test_name)	\
	pr_info(test_name " test passed")

#define pr_expect_warn(test_name)	\
	pr_info(test_name " - it should WARN")

/* wrapper for __is_pmalloc_addr() with messages */
static inline bool validate_alloc(bool expected, void *addr)
{
	bool test;

	test = __is_pmalloc_addr(addr);
	pr_info("must be %s: %s",
		  expected ? "ok" : "nok", test  ? "ok" : "nok");
	return likely(test == expected);
}

#define is_alloc_ok(variable)	\
	validate_alloc(true, variable)


#define is_alloc_nok(variable)	\
	validate_alloc(false, variable)

/* --------------- tests the basic life-cycle of a pool --------------- */
static bool create_and_destroy_pool(void)
{
	static struct pmalloc_pool *pool;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	pmalloc_destroy_pool(pool);
	pr_success("pool creation and destruction");
	return true;
}

/*  verifies that it's possible to allocate from the pool */
static bool test_alloc(void)
{
	static struct pmalloc_pool *pool;
	static void *p;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	p = pmalloc(pool,  SIZE_1 - 1);
	pmalloc_destroy_pool(pool);
	if (WARN(!p, MSG_NO_PMEM))
		return false;
	pr_success("allocation capability");
	return true;
}

/* tests the identification of pmalloc ranges */
static bool test_is_pmalloc_object(void)
{
	struct pmalloc_pool *pool;
	void *pmalloc_p;
	void *vmalloc_p;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	vmalloc_p = vmalloc(1);
	if (WARN(!vmalloc_p, "Cannot allocate vmalloc memory."))
		goto error_vmalloc;
	pmalloc_p = pmalloc(pool, 1);
	if (WARN(!pmalloc_p, MSG_NO_PMEM))
		goto error;
	if (WARN_ON(!(is_alloc_ok(pmalloc_p) &&
		      is_alloc_nok(vmalloc_p))))
		goto error;
	retval = true;
	pr_success("is_pmalloc_object");
error:
	vfree(vmalloc_p);
error_vmalloc:
	pmalloc_destroy_pool(pool);
	return retval;
}

#define INSERT_OFFSET (PAGE_SIZE * 3 / 2)
#define INSERT_SIZE (PAGE_SIZE * 2)
#define REGION_SIZE (PAGE_SIZE * 5)
/* Verify rare writes across multiple pages, unaligned to PAGE_SIZE. */
static bool test_pmalloc_wr_array(void)
{
	struct pmalloc_pool *pool;
	char *region;
	char *mod;
	unsigned int i;
	int retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	region = pzalloc(pool, REGION_SIZE);
	if (WARN(!region, MSG_NO_PMEM))
		goto destroy_pool;
	mod = vmalloc(INSERT_SIZE);
	if (WARN(!mod, "Failed to allocate memory from vmalloc"))
		goto destroy_pool;
	memset(mod, 0xA5, INSERT_SIZE);
	pmalloc_protect_pool(pool);
	retval = !pmalloc_wr_array(pool, region + INSERT_OFFSET,
					   mod, INSERT_SIZE);
	if (WARN(retval, "wr_array failed"))
		goto free_mod;

	for (i = 0; i < REGION_SIZE; i++)
		if (i >= INSERT_OFFSET &&
		    i < (INSERT_SIZE + INSERT_OFFSET)) {
			if (WARN(region[i] != (char)0xA5,
				 "Failed to alter target area"))
				goto free_mod;
		} else {
			if (WARN(region[i] != 0,
				 "Unexpected alteration outside region"))
				goto free_mod;
		}
	retval = true;
	pr_success("pmalloc_wr");
free_mod:
	vfree(mod);
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

/* ---------------- tests the allowed write actions ------------------- */
#define TEST_ARRAY_SIZE 5
#define TEST_ARRAY_TARGET (TEST_ARRAY_SIZE / 2)

static bool test_pmalloc_wr_char(void)
{
	struct pmalloc_pool *pool;
	char *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(char) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = (char)0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_char(pool, array + TEST_ARRAY_TARGET,
					  (char)0x5A),
		 "Failed to alter char variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ?
				      (char)0x5A : (char)0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_char");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_short(void)
{
	struct pmalloc_pool *pool;
	short *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(short) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = (short)0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_short(pool, array + TEST_ARRAY_TARGET,
					   (short)0x5A),
		 "Failed to alter short variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ?
				      (short)0x5A : (short)0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_short");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_ushort(void)
{
	struct pmalloc_pool *pool;
	unsigned short *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(unsigned short) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = (unsigned short)0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_ushort(pool, array + TEST_ARRAY_TARGET,
					    (unsigned short)0x5A),
		 "Failed to alter unsigned short variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ?
				      (unsigned short)0x5A :
				      (unsigned short)0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_ushort");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_int(void)
{
	struct pmalloc_pool *pool;
	int *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(int) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_int(pool, array + TEST_ARRAY_TARGET,
					 0x5A),
		 "Failed to alter int variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_int");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_uint(void)
{
	struct pmalloc_pool *pool;
	unsigned int *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(unsigned int) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_uint(pool, array + TEST_ARRAY_TARGET,
					  0x5A),
		 "Failed to alter unsigned int variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_uint");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_long(void)
{
	struct pmalloc_pool *pool;
	long *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(long) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_long(pool, array + TEST_ARRAY_TARGET,
					  0x5A),
		 "Failed to alter long variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_long");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_ulong(void)
{
	struct pmalloc_pool *pool;
	unsigned long *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(unsigned long) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_ulong(pool, array + TEST_ARRAY_TARGET,
					   0x5A),
		 "Failed to alter unsigned long variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_ulong");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_longlong(void)
{
	struct pmalloc_pool *pool;
	long long *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(long long) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_longlong(pool,
					      array + TEST_ARRAY_TARGET,
					      0x5A),
		 "Failed to alter long variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_longlong");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_ulonglong(void)
{
	struct pmalloc_pool *pool;
	unsigned long long *array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(unsigned long long) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = 0xA5;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_ulonglong(pool,
					       array + TEST_ARRAY_TARGET,
					       0x5A),
		 "Failed to alter unsigned long long variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ? 0x5A : 0xA5),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_ulonglong");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_pmalloc_wr_ptr(void)
{
	struct pmalloc_pool *pool;
	int **array;
	unsigned int i;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	array = pmalloc(pool, sizeof(int *) * TEST_ARRAY_SIZE);
	if (WARN(!array, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		array[i] = NULL;
	pmalloc_protect_pool(pool);
	if (WARN(!pmalloc_wr_ptr(pool, array + TEST_ARRAY_TARGET,
					       array),
		 "Failed to alter ptr variable"))
		goto destroy_pool;
	for (i = 0; i < TEST_ARRAY_SIZE; i++)
		if (WARN(array[i] != (i == TEST_ARRAY_TARGET ?
				      (void *)array : NULL),
			 "Unexpected value in test array."))
			goto destroy_pool;
	retval = true;
	pr_success("wr_ptr");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;

}

static bool test_specialized_wrs(void)
{
	if (WARN(!(test_pmalloc_wr_char() &&
		   test_pmalloc_wr_short() &&
		   test_pmalloc_wr_ushort() &&
		   test_pmalloc_wr_int() &&
		   test_pmalloc_wr_uint() &&
		   test_pmalloc_wr_long() &&
		   test_pmalloc_wr_ulong() &&
		   test_pmalloc_wr_longlong() &&
		   test_pmalloc_wr_ulonglong() &&
		   test_pmalloc_wr_ptr()),
		 "specialized rare writesled"))
		return false;
	pr_success("specialized rare writes");
	return true;

}

/* -------------- tests that forbidden writes are caught -------------- */

/* Test that rare write will refuse to operate on RO pools. */
static bool test_illegal_wr_ro_pool(void)
{
	struct pmalloc_pool *pool;
	int *var_ptr;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	var_ptr = pmalloc(pool, sizeof(int));
	if (WARN(!var_ptr, MSG_NO_PMEM))
		goto destroy_pool;
	*var_ptr = 0xA5;
	pmalloc_protect_pool(pool);
	pr_expect_warn("Illegal wr to R/O pool");
	if (WARN(pmalloc_wr_int(pool, var_ptr, 0x5A),
		 "Unexpected successful write to R/O protected pool"))
		goto destroy_pool;
	retval = true;
	pr_success("Illegal wr to RO pool");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

/* Test that pmalloc rare write will refuse to operate on static memory. */
static int wr_data __wr_after_init = 0xA5;
static bool test_illegal_wr_static_wr_mem(void)
{
	struct pmalloc_pool *pool;
	int *dummy;
	bool write_result;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	dummy = pmalloc(pool, sizeof(*dummy));
	if (WARN(!dummy, MSG_NO_PMEM)) {
		pmalloc_destroy_pool(pool);
		return false;
	}
	*dummy = 1;
	pmalloc_protect_pool(pool);
	pr_expect_warn("Illegal wr to static memory");
	write_result = pmalloc_wr_int(pool, &wr_data, 0x5A);
	pmalloc_destroy_pool(pool);
	if (WARN(write_result || wr_data != 0xA5,
		 "Unexpected successful write to static memory"))
		return false;
	pr_success("Illegal wr to static memory");
	return true;
}

/* Test that pmalloc rare write will refuse to operate on const. */
static const int const_data = 0xA5;
static bool test_illegal_wr_const(void)
{
	struct pmalloc_pool *pool;
	int *dummy;
	bool write_result;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	dummy = pmalloc(pool, sizeof(*dummy));
	if (WARN(!dummy, MSG_NO_PMEM)) {
		pmalloc_destroy_pool(pool);
		return false;
	}
	*dummy = 1;
	pmalloc_protect_pool(pool);
	pr_expect_warn("Illegal wr to const");
	write_result = pmalloc_wr_int(pool, &const_data, 0x5A);
	pmalloc_destroy_pool(pool);
	if (WARN(write_result || const_data != 0xA5,
		 "Unexpected successful write to const memory"))
		return false;
	pr_success("Illegal wr to const memory");
	return true;
}

/* Test that pmalloc rare write will refuse to operate on ro_after_init. */
static int ro_after_init_data __ro_after_init = 0xA5;
static bool test_illegal_wr_ro_after_init(void)
{
	struct pmalloc_pool *pool;
	int *dummy;
	bool write_result;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	dummy = pmalloc(pool, sizeof(*dummy));
	if (WARN(!dummy, MSG_NO_PMEM)) {
		pmalloc_destroy_pool(pool);
		return false;
	}
	*dummy = 1;
	pmalloc_protect_pool(pool);
	pr_expect_warn("Illegal wr to ro_after_init");
	write_result = pmalloc_wr_int(pool, &ro_after_init_data, 0x5A);
	pmalloc_destroy_pool(pool);
	if (WARN(write_result || ro_after_init_data != 0xA5,
		 "Unexpected successful write to ro_after_init memory"))
		return false;
	pr_success("Illegal wr to ro_after_init memory");
	return true;
}

static bool test_illegal_wrs(void)
{
	if (WARN(!(test_illegal_wr_ro_after_init() &&
		   test_illegal_wr_static_wr_mem() &&
		   test_illegal_wr_const() &&
		   test_illegal_wr_ro_pool()),
		 "Illegal rare writes tests failed"))
		return false;
	pr_success("Illegal rare writes");
	return true;
}

/* ----------------------- tests self protection ----------------------- */

static bool test_auto_ro(void)
{
	struct pmalloc_pool *pool;
	int *first_chunk;
	int *second_chunk;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_AUTO_RO);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	first_chunk = (int *)pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	if (WARN(!first_chunk, MSG_NO_PMEM))
		goto error;
	second_chunk = (int *)pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	if (WARN(!second_chunk, MSG_NO_PMEM))
		goto error;
	if (WARN(!pmalloc_is_address_protected(first_chunk),
		 "Failed to automatically write protect exhausted vmarea"))
		goto error;
	pr_success("AUTO_RO");
	retval = true;
error:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_auto_wr(void)
{
	struct pmalloc_pool *pool;
	int *first_chunk;
	int *second_chunk;
	bool retval = false;

	pool = pmalloc_create_pool(PMALLOC_AUTO_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	first_chunk = (int *)pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	if (WARN(!first_chunk, MSG_NO_PMEM))
		goto error;
	second_chunk = (int *)pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	if (WARN(!second_chunk, MSG_NO_PMEM))
		goto error;
	if (WARN(!pmalloc_is_address_protected(first_chunk),
		 "Failed to automatically write protect exhausted vmarea"))
		goto error;
	pr_success("AUTO_WR");
	retval = true;
error:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_start_wr(void)
{
	struct pmalloc_pool *pool;
	int *chunks[2];
	bool retval = false;
	int i;

	pool = pmalloc_create_pool(PMALLOC_START_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	for (i = 0; i < 2; i++) {
		chunks[i] = (int *)pmalloc(pool, 1);
		if (WARN(!chunks[i], MSG_NO_PMEM))
			goto error;
		if (WARN(!pmalloc_is_address_protected(chunks[i]),
			 "vmarea was not protected from the start"))
			goto error;
	}
	if (WARN(vmalloc_to_page(chunks[0]) != vmalloc_to_page(chunks[1]),
		 "START: mostly empty vmap area not reused"))
		goto error;
	pr_success("START");
	retval = true;
error:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_self_protection(void)
{
	if (WARN(!(test_auto_ro() &&
		   test_auto_wr() &&
		   test_start_wr()),
		 "self protection tests failed"))
		return false;
	pr_success("self protection");
	return true;
}

/*
 * test_pmalloc()  -main entry point for running the test cases
 */
static int __init test_pmalloc_init_module(void)
{
	if (WARN(!(create_and_destroy_pool() &&
		   test_alloc() &&
		   test_is_pmalloc_object() &&
		   test_pmalloc_wr_array() &&
		   test_specialized_wrs() &&
		   test_illegal_wrs() &&
		   test_self_protection()),
		 "protected memory allocator test failed"))
		return -EFAULT;
	pr_success("protected memory allocator");
	return 0;
}

module_init(test_pmalloc_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for pmalloc.");
