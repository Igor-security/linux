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

/*
 * Note: __ro_after_init data is, for every practical effect, equivalent to
 * const data, since they are even write protected at the same time; there
 * is no need for separate testing.
 * __wr_after_init data, otoh, is altered also after the write protection
 * takes place and it cannot be exploitable for altering more permanent
 * data.
 */

static const int rodata_test_data = INIT_TEST_VAL;
static int wr_after_init_test_data __wr_after_init = INIT_TEST_VAL;
extern long __start_wr_after_init;
extern long __end_wr_after_init;

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
	return true;
}

void rodata_test(void)
{
	if (test_data("rodata", &rodata_test_data,
		      (unsigned long)&__start_rodata,
		      (unsigned long)&__end_rodata) &&
	    test_data("wr after init data", &wr_after_init_test_data,
		      (unsigned long)&__start_wr_after_init,
		      (unsigned long)&__end_wr_after_init))
		pr_info("all tests were successful\n");
}
