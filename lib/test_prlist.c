// SPDX-License-Identifier: GPL-2.0
/*
 * test_prlist.c
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <linux/prlist.h>


#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

//struct test_data {
//	int data_int;
//	struct prot_head list;
//	unsigned long long data_ulong;
//};
//
//static int test_prot_list(void)
//{
//	struct prot_list_pool *pool;
//	struct prot_head *head;
//	struct list_head *cursor;
//	struct test_data data;
//	int i;
//
//	/* Create a pool for the protectable list. */
//	pool = prot_list_create_pool();
//	if (WARN(!pool, "could not create pool"))
//		return -ENOMEM;
//
//	head = PROT_LIST_HEAD(pool);
//	for (i = 0; i < 100; i++) {
//		data.data_int = i;
//		data.data_ulong = i * i;
//		if (i % 2)
//			prot_list_append(pool, head, &data, list);
//		else
//			prot_list_prepend(pool, head, &data, list);
//	}
//	for (cursor = head->list.next; cursor != &head->list; cursor = cursor->next) {
//		struct test_data *data;
//
//		data = container_of(list_to_prot(cursor), struct test_data, list);
//
//		pr_info("cursor: 0x%08lx  data_int: %02d ",
//			(unsigned long)cursor, data->data_int);
//	}
//	for (cursor = head->list.prev; cursor != &head->list; cursor = cursor->prev) {
//		struct test_data *data;
//
//		data = container_of(list_to_prot(cursor), struct test_data, list);
//
//		pr_info("cursor: 0x%08lx  data_int: %02d ",
//			(unsigned long)cursor, data->data_int);
//	}
//	return 0;
//}
//
//
//void pippo(void)
//{
//	struct pmalloc_pool *pool;
//	int *v1;
//	int *v2;
//	int *v3;
//
//	pool = pmalloc_create_pool(PMALLOC_RW);
//	pr_info("pool->align: %lu", pool->align);
//	v1 = pmalloc(pool, sizeof(int));
//	v2 = pmalloc(pool, sizeof(int));
//	v3 = pmalloc(pool, sizeof(int));
//	pr_info("v1: %lu 0x%016lx", sizeof(int), v1);
//	pr_info("v2: %lu 0x%016lx", sizeof(int), v2);
//	pr_info("v3: %lu 0x%016lx", sizeof(int), v3);
//}


struct test_data {
	int data_int;
	struct prlist_head list;
	unsigned long long data_ulong;
};


struct prlist_head *test_head __rare_write_after_init;
void test_prlist(void)
{
	struct prlist_pool *pool;

	pool = prlist_create_pool();
	INIT_STATIC_PRLIST_HEAD(pool, &test_head);
	if (&test_head->list == test_head->list.prev &&
	    &test_head->list == test_head->list.next)
		pr_info("QQQQQ OK");
	else
		pr_info("QQQQQ NO");

}

/**
 * test_prlist()  -main entry point for running the test cases
 */
static int __init test_prlist_init_module(void)
{
	test_prlist();
	return 0;
}

module_init(test_pmalloc_init_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for protectable list.");
