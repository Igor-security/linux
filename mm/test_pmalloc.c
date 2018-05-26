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
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/prmemextra.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define SIZE_1 (PAGE_SIZE * 3)
#define SIZE_2 1000

static const char MSG_NO_POOL[] = "Cannot allocate memory for the pool.";
static const char MSG_NO_PMEM[] = "Cannot allocate memory from the pool.";

#define pr_success(test_name)	\
	pr_info(test_name " test passed")

/* --------------- tests the basic life-cycle of a pool --------------- */

static bool is_address_protected(void *p)
{
	struct page *page;
	struct vmap_area *area;

	if (unlikely(!is_vmalloc_addr(p)))
		return false;
	page = vmalloc_to_page(p);
	if (unlikely(!page))
		return false;
	wmb(); /* Flush changes to the page table - is it needed? */
	area = find_vmap_area((uintptr_t)p);
	if (unlikely((!area) || (!area->vm) ||
		     ((area->vm->flags & VM_PMALLOC_PROTECTED_MASK) !=
		      VM_PMALLOC_PROTECTED_MASK)))
		return false;
	return true;
}

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
	if (WARN(!is_address_protected(first_chunk),
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
	if (WARN(!is_address_protected(first_chunk),
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
		if (WARN(!is_address_protected(chunks[i]),
			 "vmarea was not protected from the start"))
			goto error;
	}
	if (WARN(vmalloc_to_page(chunks[0]) != vmalloc_to_page(chunks[1]),
		 "START_WR: mostly empty vmap area not reused"))
		goto error;
	pr_success("START_WR");
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

/* ----------------- tests basic write rare functions ----------------- */

#define INSERT_OFFSET (PAGE_SIZE * 3 / 2)
#define INSERT_SIZE (PAGE_SIZE * 2)
#define REGION_SIZE (PAGE_SIZE * 5)
static bool test_wr_memset(void)
{
	struct pmalloc_pool *pool;
	char *region;
	unsigned int i;
	int retval = false;

	pool = pmalloc_create_pool(PMALLOC_START_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	region = pzalloc(pool, REGION_SIZE);
	if (WARN(!region, MSG_NO_PMEM))
		goto destroy_pool;
	for (i = 0; i < REGION_SIZE; i++)
		if (WARN(region[i], "Failed to memset wr memory"))
			goto destroy_pool;
	retval = !wr_memset(region + INSERT_OFFSET, 1, INSERT_SIZE);
	if (WARN(retval, "wr_memset failed"))
		goto destroy_pool;
	for (i = 0; i < REGION_SIZE; i++)
		if (i >= INSERT_OFFSET &&
		    i < (INSERT_SIZE + INSERT_OFFSET)) {
			if (WARN(!region[i],
				 "Failed to alter target area"))
				goto destroy_pool;
		} else {
			if (WARN(region[i] != 0,
				 "Unexpected alteration outside region"))
				goto destroy_pool;
		}
	retval = true;
	pr_success("wr_memset");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_wr_strdup(void)
{
	const char src[] = "Some text for testing pstrdup()";
	struct pmalloc_pool *pool;
	char *dst;
	int retval = false;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return false;
	dst = pstrdup(pool, src);
	if (WARN(!dst || strcmp(src, dst), "pmalloc wr strdup failed"))
		goto destroy_pool;
	retval = true;
	pr_success("pmalloc wr strdup");
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

/* Verify write rare across multiple pages, unaligned to PAGE_SIZE. */
static bool test_wr_copy(void)
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
	retval = !wr_memcpy(region + INSERT_OFFSET, mod, INSERT_SIZE);
	if (WARN(retval, "wr_copy failed"))
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
	pr_success("wr_copy");
free_mod:
	vfree(mod);
destroy_pool:
	pmalloc_destroy_pool(pool);
	return retval;
}

/* ----------------- tests specialized write actions ------------------- */

#define TEST_ARRAY_SIZE 5
#define TEST_ARRAY_TARGET (TEST_ARRAY_SIZE / 2)

static bool test_wr_char(void)
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
	if (WARN(!wr_char(array + TEST_ARRAY_TARGET, (char)0x5A),
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

static bool test_wr_short(void)
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
	if (WARN(!wr_short(array + TEST_ARRAY_TARGET, (short)0x5A),
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

static bool test_wr_ushort(void)
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
	if (WARN(!wr_ushort(array + TEST_ARRAY_TARGET,
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

static bool test_wr_int(void)
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
	if (WARN(!wr_int(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_uint(void)
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
	if (WARN(!wr_uint(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_long(void)
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
	if (WARN(!wr_long(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_ulong(void)
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
	if (WARN(!wr_ulong(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_longlong(void)
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
	if (WARN(!wr_longlong(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_ulonglong(void)
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
	if (WARN(!wr_ulonglong(array + TEST_ARRAY_TARGET, 0x5A),
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

static bool test_wr_ptr(void)
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
	if (WARN(!wr_ptr(array + TEST_ARRAY_TARGET, array),
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
	if (WARN(!(test_wr_char() &&
		   test_wr_short() &&
		   test_wr_ushort() &&
		   test_wr_int() &&
		   test_wr_uint() &&
		   test_wr_long() &&
		   test_wr_ulong() &&
		   test_wr_longlong() &&
		   test_wr_ulonglong() &&
		   test_wr_ptr()),
		 "specialized write rare failed"))
		return false;
	pr_success("specialized write rare");
	return true;

}

/*
 * test_pmalloc()  -main entry point for running the test cases
 */
static int __init test_pmalloc_init_module(void)
{
	if (WARN(!(create_and_destroy_pool() &&
		   test_alloc() &&
		   test_self_protection() &&
		   test_wr_memset() &&
		   test_wr_strdup() &&
		   test_wr_copy() &&
		   test_specialized_wrs()),
		 "protected memory allocator test failed"))
		return -EFAULT;
	pr_success("protected memory allocator");
	return 0;
}

module_init(test_pmalloc_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for pmalloc.");
