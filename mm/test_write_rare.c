// SPDX-License-Identifier: GPL-2.0
/*
 * test_write_rare.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Caveat: the tests which perform modifications are run *during* init, so
 * the memory they use could be still altered through a direct write
 * operation. But the purpose of these tests is to confirm that the
 * modification through remapping works correctly. This doesn't depend on
 * the read/write status of the original mapping.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/write_rare.h>

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define pr_success(test_name)	\
	pr_info(test_name " test passed")

static int scalar __wr_after_init = 0xA5A5;

/* The section must occupy a non-zero number of whole pages */
static bool test_alignment(void)
{
	size_t pstart = (size_t)&__start_wr_after_init;
	size_t pend = (size_t)&__end_wr_after_init;

	if (WARN((pstart & ~PAGE_MASK) || (pend & ~PAGE_MASK) ||
		 (pstart >= pend), "Boundaries test failed."))
		return false;
	pr_success("Boundaries");
	return true;
}

/* Alter a scalar value */
static bool test_simple_write(void)
{
	int new_val = 0x5A5A;

	if (WARN(!wr_check_boundaries(&scalar, sizeof(scalar)),
		 "The __wr_after_init modifier did NOT work."))
		return false;

	if (WARN(!wr(&scalar, &new_val) || scalar != new_val,
		 "Scalar write rare test failed"))
		return false;

	pr_success("Scalar write rare");
	return true;
}

#define LARGE_SIZE (PAGE_SIZE * 5)
#define CHANGE_SIZE (PAGE_SIZE * 2)
#define CHANGE_OFFSET (PAGE_SIZE / 2)

static char large[LARGE_SIZE] __wr_after_init;


/* Alter data across multiple pages */
static bool test_cross_page_write(void)
{
	unsigned int i;
	char *src;
	bool check;

	src = vmalloc(PAGE_SIZE * 2);
	if (WARN(!src, "could not allocate memory"))
		return false;

	for (i = 0; i < LARGE_SIZE; i++)
		large[i] = 0xA5;

	for (i = 0; i < CHANGE_SIZE; i++)
		src[i] = 0x5A;

	check = wr_array(large + CHANGE_OFFSET, src, CHANGE_SIZE);
	vfree(src);
	if (WARN(!check, "The wr_array() failed"))
		return false;

	for (i = CHANGE_OFFSET; i < CHANGE_OFFSET + CHANGE_SIZE; i++)
		if (WARN(large[i] != 0x5A,
			 "Cross-page write rare test failed"))
			return false;

	pr_success("Cross-page write rare");
	return true;
}


#define INIT_VAL 1
#define END_VAL 4

/* Various tests for the shorthands provided for standard types. */
static char char_var __wr_after_init = INIT_VAL;
static bool test_char(void)
{
	return wr_char(&char_var, END_VAL) && char_var == END_VAL;
}

static short short_var __wr_after_init = INIT_VAL;
static bool test_short(void)
{
	return wr_short(&short_var, END_VAL) &&
		short_var == END_VAL;
}

static unsigned short ushort_var __wr_after_init = INIT_VAL;
static bool test_ushort(void)
{
	return wr_ushort(&ushort_var, END_VAL) &&
		ushort_var == END_VAL;
}

static int int_var __wr_after_init = INIT_VAL;
static bool test_int(void)
{
	return wr_int(&int_var, END_VAL) &&
		int_var == END_VAL;
}

static unsigned int uint_var __wr_after_init = INIT_VAL;
static bool test_uint(void)
{
	return wr_uint(&uint_var, END_VAL) &&
		uint_var == END_VAL;
}

static long int long_var __wr_after_init = INIT_VAL;
static bool test_long(void)
{
	return wr_long(&long_var, END_VAL) &&
		long_var == END_VAL;
}

static unsigned long int ulong_var __wr_after_init = INIT_VAL;
static bool test_ulong(void)
{
	return wr_ulong(&ulong_var, END_VAL) &&
		ulong_var == END_VAL;
}

static long long int longlong_var __wr_after_init = INIT_VAL;
static bool test_longlong(void)
{
	return wr_longlong(&longlong_var, END_VAL) &&
		longlong_var == END_VAL;
}

static unsigned long long int ulonglong_var __wr_after_init = INIT_VAL;
static bool test_ulonglong(void)
{
	return wr_ulonglong(&ulonglong_var, END_VAL) &&
		ulonglong_var == END_VAL;
}

static int referred_value = INIT_VAL;
static int *reference __wr_after_init;
static bool test_ptr(void)
{
	return wr_ptr(&reference, &referred_value) &&
		reference == &referred_value;
}

static bool test_specialized_write_rares(void)
{
	if (WARN(!(test_char() && test_short() &&
		   test_ushort() && test_int() &&
		   test_uint() && test_long() && test_ulong() &&
		   test_long() && test_ulong() &&
		   test_longlong() && test_ulonglong() &&
		   test_ptr()),
		 "Specialized write rare test failed"))
		return false;
	pr_success("Specialized write rare");
	return true;
}

/* Verify that write rare will not work against read-only memory. */
static int ro_after_init_data __ro_after_init = INIT_VAL;
static bool test_illegal_wr_ro_after_init(void)
{
	pr_info("Illegal write rare to const memory - it should WARN");
	if (WARN(wr_int(&ro_after_init_data, END_VAL) ||
		 ro_after_init_data == END_VAL,
		 "Unexpected successful write to const memory"))
		return false;
	pr_success("Illegal write rare to const memory");
	return true;
}

/*
 * "volatile" to force the compiler to not optimize away the reading back.
 * Is there a better way to do it, than using volatile?
 */
static volatile const int const_data = INIT_VAL;
static bool test_illegal_wr_const(void)
{
	pr_info("Illegal write rare to __ro_after_init - it should WARN");
	if (WARN(wr_int((int *)&const_data, END_VAL) ||
		 const_data == END_VAL,
		 "Unexpected successful write to __ro_after_init memory"))
		return false;
	pr_success("Illegal write rare to __ro_after_init memory");
	return true;
}

static bool test_illegal_write_rares(void)
{
	if (WARN(!(test_illegal_wr_ro_after_init() &&
		   test_illegal_wr_const()),
		 "Illegal static write rares test failed"))
		return false;
	pr_success("Illegal static write rares");
	return true;
}

static int __init test_static_wr_init_module(void)
{
	if (WARN(!(test_alignment() &&
		   test_simple_write() &&
		   test_cross_page_write() &&
		   test_specialized_write_rares() &&
		   test_illegal_write_rares()),
		 "static rare-write test failed"))
		return -EFAULT;
	pr_success("static write_rare");
	return 0;
}

module_init(test_static_wr_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for static write rare.");
