/*
 * rodata_test.c: functional test for mark_rodata_ro function
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#define pr_fmt(fmt) "rodata_test: " fmt

#include <linux/uaccess.h>
#include <asm/sections.h>

#define INIT_TEST_VAL 0xC3

static const int rodata_test_data = INIT_TEST_VAL;

static bool test_data(char *data_type, const int *data,
		      unsigned long start, unsigned long end)
{
	int zero = 0;

	/* test 1: read the value */
	/* If this test fails, some previous testrun has clobbered the state */
	if (*data != INIT_TEST_VAL) {
		pr_err("%s: test 1 fails (init data value)\n", data_type);
		return false;
	}

	/* test 2: write to the variable; this should fault */
	if (!probe_kernel_write((void *)data, (void *)&zero, sizeof(zero))) {
		pr_err("%s: test data was not read only\n", data_type);
		return false;
	}

	/* test 3: check the value hasn't changed */
	if (*data != INIT_TEST_VAL) {
		pr_err("%s: test data was changed\n", data_type);
		return false;
	}

	/* test 4: check if the rodata section is PAGE_SIZE aligned */
	if (start & (PAGE_SIZE - 1)) {
		pr_err("%s: start of data is not page size aligned\n",
		       data_type);
		return false;
	}
	if (end & (PAGE_SIZE - 1)) {
		pr_err("%s: end of data is not page size aligned\n",
		       data_type);
		return false;
	}
	pr_info("%s tests were successful", data_type);
	return true;
}

void rodata_test(void)
{
	test_data("rodata", &rodata_test_data,
		  (unsigned long)&__start_rodata,
		  (unsigned long)&__end_rodata);
}
