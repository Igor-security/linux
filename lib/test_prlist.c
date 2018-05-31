// SPDX-License-Identifier: GPL-2.0
/*
 * test_prlist.c: Test cases for protected doubly linked list
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

static struct prlist_pool *pool;

static struct prlist_head test_head __rare_write_after_init;
static bool test_init_prlist_head(void)
{
	INIT_STATIC_PRLIST_HEAD(&test_head);
	if (WARN(&test_head.list != test_head.list.prev ||
		 &test_head.list != test_head.list.next,
		 "initialization of static prlist_head failed"))
		return false;
	pr_info("initialization of static prlist_head passed");
	return true;
}


struct test_data {
	int d_int;
	struct prlist_head list;
	unsigned long long d_ulonglong;
};

#define LIST_NODES 5
static bool test_build_prlist(void)
{
	short i;
	struct test_data *node;
	struct list_head *cursor;
	int delta;

	pool = prlist_create_pool();
	if (WARN(!pool, "could not create pool"))
		return false;

	for (i = 0; i < LIST_NODES; i++) {
		node = (struct test_data *)pmalloc(&pool->pool,
						   sizeof(*node));
		if (WARN(!node, "Failed to allocate list node"))
			goto out;
		pmalloc_rare_write_int(&pool->pool, &node->d_int, i);
		pmalloc_rare_write_ulonglong(&pool->pool,
					     &node->d_ulonglong, i);
		prlist_add_tail(pool, &node->list, &test_head);
	}
	for (i = 1; i < LIST_NODES; i++) {
		node = (struct test_data *)pmalloc(&pool->pool,
						   sizeof(*node));
		if (WARN(!node, "Failed to allocate list node"))
			goto out;
		pmalloc_rare_write_int(&pool->pool, &node->d_int, i);
		pmalloc_rare_write_ulonglong(&pool->pool,
					     &node->d_ulonglong, i);
		prlist_add(pool, &node->list, &test_head);
	}
	i = LIST_NODES;
	delta = -1;
	list_for_each(cursor, &test_head.list) {
		struct prlist_head *head;

		i += delta;
		if (!i)
			delta = 1;
		head = list_to_prlist(cursor);
		node = container_of(head, struct test_data, list);
		if (WARN(node->d_int != i || node->d_ulonglong != i,
			 "unexpected value in list, build test failed"))
			goto out;
	}
	pr_info("build list test passed");
	return true;
out:
	prlist_destroy_pool(pool);
	return false;
}

static bool test_teardown_prlist(void)
{
	short i;

	for (i = 0; !list_empty(&test_head.list); i++)
		prlist_del_entry(pool, list_to_prlist(test_head.list.next));
	if (WARN(i != LIST_NODES * 2 - 1, "teardown test failed"))
		return false;
	prlist_destroy_pool(pool);
	pr_info("teardown test passed");
	return true;
}

static int __init test_prlist_init_module(void)
{
	if (WARN(!(test_init_prlist_head() &&
		   test_build_prlist() &&
		   test_teardown_prlist()),
		 "protected list testing failed"))
		return -EFAULT;
	pr_info("protected list testing passed");
	return 0;
}

module_init(test_prlist_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for protected doubly linked list.");
