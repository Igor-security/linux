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

static struct pmalloc_pool *pool;

static struct prlist_head test_prlist_head __rare_write_after_init =
	PRLIST_HEAD_INIT(test_prlist_head.list);

static bool test_init_prlist_head(void)
{
	if (WARN(test_prlist_head.prev != &test_prlist_head ||
		 test_prlist_head.next != &test_prlist_head,
		 "static initialization of static prlist_head failed"))
		return false;
	rare_write_ptr(&test_prlist_head.next, NULL);
	rare_write_ptr(&test_prlist_head.prev, NULL);
	if (WARN(test_prlist_head.prev || test_prlist_head.next,
		 "resetting of static prlist_head failed"))
		return false;
	INIT_STATIC_PRLIST_HEAD(&test_prlist_head);
	if (WARN(test_prlist_head.prev != &test_prlist_head ||
		 test_prlist_head.next != &test_prlist_head,
		 "initialization of static prlist_head failed"))
		return false;
	pr_info("initialization of static prlist_head passed");
	return true;
}


struct prlist_data {
	int d_int;
	struct prlist_head list;
	unsigned long long d_ulonglong;
};

#define LIST_NODES 5
static bool test_build_prlist(void)
{
	short i;
	struct prlist_data *node;
	int delta;

	pool = prlist_create_pool();
	if (WARN(!pool, "could not create pool"))
		return false;

	for (i = 0; i < LIST_NODES; i++) {
		node = (struct prlist_data *)pmalloc(pool, sizeof(*node));
		if (WARN(!node, "Failed to allocate list node"))
			goto out;
		pmalloc_rare_write_int(pool, &node->d_int, i);
		pmalloc_rare_write_ulonglong(pool, &node->d_ulonglong, i);
		prlist_add_tail(pool, &node->list, &test_prlist_head);
	}
	for (i = 1; i < LIST_NODES; i++) {
		node = (struct prlist_data *)pmalloc(pool, sizeof(*node));
		if (WARN(!node, "Failed to allocate list node"))
			goto out;
		pmalloc_rare_write_int(pool, &node->d_int, i);
		pmalloc_rare_write_ulonglong(pool, &node->d_ulonglong, i);
		prlist_add(pool, &node->list, &test_prlist_head);
	}
	i = LIST_NODES;
	delta = -1;
	list_for_each_entry(node, &test_prlist_head, list) {
		i += delta;
		if (!i)
			delta = 1;
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

	for (i = 0; !list_empty(&test_prlist_head.list); i++)
		prlist_del_entry(pool, test_prlist_head.next);
	if (WARN(i != LIST_NODES * 2 - 1, "teardown test failed"))
		return false;
	prlist_destroy_pool(pool);
	pr_info("teardown test passed");
	return true;
}

static bool test_prlist(void)
{
	if (WARN(!(test_init_prlist_head() &&
		   test_build_prlist() &&
		   test_teardown_prlist()),
		 "protected list test failed"))
		return false;
	pr_info("protected list test passed");
	return true;
}

static struct prhlist_head test_prhlist_head __rare_write_after_init =
	PRHLIST_HEAD_INIT;

static bool test_init_prhlist_head(void)
{
	if (WARN(test_prhlist_head.first,
		 "static initialization of static prhlist_head failed"))
		return false;
	rare_write_ptr(&test_prhlist_head.first, (void *)-1);
	if (WARN(!test_prhlist_head.first,
		 "resetting of static prhlist_head failed"))
		return false;
	INIT_STATIC_PRHLIST_HEAD(&test_prhlist_head);
	if (WARN(!test_prlist_head.prev,
		 "initialization of static prhlist_head failed"))
		return false;
	pr_info("initialization of static prlist_head passed");
	return true;
}

static bool test_build_prhlist(void)
{
	return true;
}

static bool test_teardown_prhlist(void)
{
	return true;
}

static bool test_prhlist(void)
{
	if (WARN(!(test_init_prhlist_head() &&
		   test_build_prhlist() &&
		   test_teardown_prhlist()),
		 "protected hlist test failed"))
		return false;
	pr_info("protected hlist test passed");
	return true;
}

static int __init test_prlists_init_module(void)
{
	if (WARN(!(test_prlist() && test_prhlist()),
		 "protected lists test failed"))
		return -EFAULT;
	pr_info("protected lists test passed");
	return 0;
}

module_init(test_prlists_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for protected doubly linked list.");
