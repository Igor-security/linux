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

#define SIZE_1 (PAGE_SIZE * 3)
#define SIZE_2 1000

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

static int victim __rare_write_after_init = 23;


int test_static_rare_write(void)
{
	int src = 11;

	pr_notice("QQQQQQ Victim is %d", victim);
	rare_write(&victim, &src);
	pr_info("QQQQQQ start: 0x%016lx", (unsigned long)&__start_rare_write_after_init);
	pr_info("QQQQQQ victim: 0x%016lx", (unsigned long)&victim);
	pr_info("QQQQQQ end: 0x%016lx", (unsigned long)&__end_rare_write_after_init);
	pr_notice("QQQQQQ Victim is %d", victim);
}

static int __init test_static_rare_write_init_module(void)
{
	pr_notice("rare write selftest");
	test_static_rare_write();

	return 0;
}

module_init(test_static_rare_write_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for static rare write.");
