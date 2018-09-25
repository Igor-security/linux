// SPDX-License-Identifier: GPL-2.0

/*
 * test_write_rare.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/prmem.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

extern long __start_wr_after_init;
extern long __end_wr_after_init;

static __wr_after_init int scalar = '0';
static __wr_after_init u8 array[PAGE_SIZE * 3] __aligned(PAGE_SIZE);

/* The section must occupy a non-zero number of whole pages */
static bool test_alignment(void)
{
	unsigned long pstart = (unsigned long)&__start_wr_after_init;
	unsigned long pend = (unsigned long)&__end_wr_after_init;

	if (WARN((pstart & ~PAGE_MASK) || (pend & ~PAGE_MASK) ||
		 (pstart >= pend), "Boundaries test failed."))
		return false;
	pr_info("Boundaries test passed.");
	return true;
}

static inline bool test_pattern(void)
{
	return (memtst(array, '0', PAGE_SIZE / 2) ||
		memtst(array + PAGE_SIZE / 2, '1', PAGE_SIZE * 3 / 4) ||
		memtst(array + PAGE_SIZE * 5 / 4, '0', PAGE_SIZE / 2) ||
		memtst(array + PAGE_SIZE * 7 / 4, '1', PAGE_SIZE * 3 / 4) ||
		memtst(array + PAGE_SIZE * 5 / 2, '0', PAGE_SIZE / 2));
}

static bool test_wr_memset(void)
{
	int new_val = '1';

	wr_memset(&scalar, new_val, sizeof(scalar));
	if (WARN(memtst(&scalar, new_val, sizeof(scalar)),
		 "Scalar write rare memset test failed."))
		return false;

	pr_info("Scalar write rare memset test passed.");

	wr_memset(array, '0', PAGE_SIZE * 3);
	if (WARN(memtst(array, '0', PAGE_SIZE * 3),
		 "Array write rare memset test failed."))
		return false;

	wr_memset(array + PAGE_SIZE / 2, '1', PAGE_SIZE * 2);
	if (WARN(memtst(array + PAGE_SIZE / 2, '1', PAGE_SIZE * 2),
		 "Array write rare memset test failed."))
		return false;

	wr_memset(array + PAGE_SIZE * 5 / 4, '0', PAGE_SIZE / 2);
	if (WARN(memtst(array + PAGE_SIZE * 5 / 4, '0', PAGE_SIZE / 2),
		 "Array write rare memset test failed."))
		return false;

	if (WARN(test_pattern(), "Array write rare memset test failed."))
		return false;

	pr_info("Array write rare memset test passed.");
	return true;
}

static u8 array_1[PAGE_SIZE * 2];
static u8 array_2[PAGE_SIZE * 2];

static bool test_wr_memcpy(void)
{
	int new_val = 0x12345678;

	wr_assign(scalar, new_val);
	if (WARN(memcmp(&scalar, &new_val, sizeof(scalar)),
		 "Scalar write rare memcpy test failed."))
		return false;
	pr_info("Scalar write rare memcpy test passed.");

	wr_memset(array, '0', PAGE_SIZE * 3);
	memset(array_1, '1', PAGE_SIZE * 2);
	memset(array_2, '0', PAGE_SIZE * 2);
	wr_memcpy(array + PAGE_SIZE / 2, array_1, PAGE_SIZE * 2);
	wr_memcpy(array + PAGE_SIZE * 5 / 4, array_2, PAGE_SIZE / 2);

	if (WARN(test_pattern(), "Array write rare memcpy test failed."))
		return false;

	pr_info("Array write rare memcpy test passed.");
	return true;
}

static __wr_after_init int *dst;
static int reference = 0x54;

static bool test_wr_rcu_assign_pointer(void)
{
	wr_rcu_assign_pointer(dst, &reference);
	return dst == &reference;
}

static int __init test_static_wr_init_module(void)
{
	pr_info("static write_rare test");
	if (WARN(!(test_alignment() &&
		   test_wr_memset() &&
		   test_wr_memcpy() &&
		   test_wr_rcu_assign_pointer()),
		 "static rare-write test failed"))
		return -EFAULT;
	pr_info("static write_rare test passed");
	return 0;
}

module_init(test_static_wr_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for static write rare.");
