// SPDX-License-Identifier: GPL-2.0
/*
 * test_rare_write.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/rare_write.h>
#include <linux/bug.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

static int scalar __rare_write_after_init = 0xA5A5;

static int test_simple_write(void)
{
	int new_val = 0x5A5A;

	if (WARN(!rare_write_check_boundaries(&scalar, sizeof(scalar)),
		 "The __rare_write_after_init modifier did NOT work."))
		return -EFAULT;

	if (WARN(!rare_write(&scalar, &new_val) || scalar != new_val,
		 "Scalar rare write test failed"))
		return -EFAULT;

	pr_info("Scalar rare-write test passed.");
	return 0;
}

#define LARGE_SIZE (PAGE_SIZE * 5)
#define CHANGE_SIZE (PAGE_SIZE * 2)
#define CHANGE_OFFSET (PAGE_SIZE / 2)

static char large[LARGE_SIZE] __rare_write_after_init;

static int test_cross_page_write(void)
{
	unsigned int i;
	char *src;
	bool check;

	src = vmalloc(PAGE_SIZE * 2);
	if (WARN(!src, "could not allocate memory"))
		return -ENOMEM;

	for (i = 0; i < LARGE_SIZE; i++)
		large[i] = 0xA5;

	for (i = 0; i < CHANGE_SIZE; i++)
		src[i] = 0x5A;

	check = rare_write_array(large + CHANGE_OFFSET, src, CHANGE_SIZE);
	vfree(src);
	if (WARN(!check, "The rare_write_array() failed"))
		return -EFAULT;

	for (i = CHANGE_OFFSET; i < CHANGE_OFFSET + CHANGE_SIZE; i++)
		if (WARN(large[i] != 0x5A,
			 "Cross-page rare-write test failed"))
			return -EFAULT;

	pr_info("Cross-page rare-write test passed");
	return 0;
}


#define INIT_VAL 1
#define END_VAL 4
static char char_var __rare_write_after_init = INIT_VAL;
static inline bool test_char(void)
{
	return rare_write_char(&char_var, END_VAL) && char_var == END_VAL;
}

static short short_var __rare_write_after_init = INIT_VAL;
static inline bool test_short(void)
{
	return rare_write_short(&short_var, END_VAL) &&
		short_var == END_VAL;
}

static unsigned short ushort_var __rare_write_after_init = INIT_VAL;
static inline bool test_ushort(void)
{
	return rare_write_ushort(&ushort_var, END_VAL) &&
		ushort_var == END_VAL;
}

static int int_var __rare_write_after_init = INIT_VAL;
static inline bool test_int(void)
{
	return rare_write_int(&int_var, END_VAL) &&
		int_var == END_VAL;
}

static unsigned int uint_var __rare_write_after_init = INIT_VAL;
static inline bool test_uint(void)
{
	return rare_write_uint(&uint_var, END_VAL) &&
		uint_var == END_VAL;
}

static long int long_var __rare_write_after_init = INIT_VAL;
static inline bool test_long(void)
{
	return rare_write_long(&long_var, END_VAL) &&
		long_var == END_VAL;
}

static unsigned long int ulong_var __rare_write_after_init = INIT_VAL;
static inline bool test_ulong(void)
{
	return rare_write_ulong(&ulong_var, END_VAL) &&
		ulong_var == END_VAL;
}

static long long int longlong_var __rare_write_after_init = INIT_VAL;
static inline bool test_longlong(void)
{
	return rare_write_longlong(&longlong_var, END_VAL) &&
		longlong_var == END_VAL;
}

static unsigned long long int ulonglong_var __rare_write_after_init = INIT_VAL;
static inline bool test_ulonglong(void)
{
	return rare_write_ulonglong(&ulonglong_var, END_VAL) &&
		ulonglong_var == END_VAL;
}

static int test_specialized_rare_writes(void)
{
	if (WARN(!(test_char() && test_short() &&
		   test_ushort() && test_int() &&
		   test_uint() && test_long() && test_ulong() &&
		   test_long() && test_ulong() &&
		   test_longlong() && test_ulonglong()),
		 "Specialized rare write test failed"))
		return -EFAULT;

	pr_info("Specialized rare write test passed");
	return 0;
}

static int __init test_static_rare_write_init_module(void)
{
	pr_notice("rare write selftest");

	if (test_simple_write() ||
	    test_cross_page_write() ||
	    test_specialized_rare_writes())
	    return -EFAULT;

	return 0;
}

module_init(test_static_rare_write_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for static rare write.");
