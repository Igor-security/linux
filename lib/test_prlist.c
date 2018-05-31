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

static struct prlist_head test_prlist_head __wr_after_init =
	PRLIST_HEAD_INIT(test_prlist_head.list);

/* ---------------------- prlist test functions ---------------------- */
static bool test_init_prlist_head(void)
{
	if (WARN(test_prlist_head.prev != &test_prlist_head ||
		 test_prlist_head.next != &test_prlist_head,
		 "static initialization of static prlist_head failed"))
		return false;
	wr_ptr(&test_prlist_head.next, NULL);
	wr_ptr(&test_prlist_head.prev, NULL);
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
	struct prlist_head node;
	unsigned long long d_ulonglong;
};


#define LIST_INTERVAL 5
#define LIST_INTERVALS 3
#define LIST_NODES (LIST_INTERVALS * LIST_INTERVAL)
static bool test_build_prlist(void)
{
	short i;
	struct prlist_data *data;
	int delta;

	pool = prlist_create_pool();
	if (WARN(!pool, "could not create pool"))
		return false;

	for (i = 0; i < LIST_NODES; i++) {
		data = (struct prlist_data *)pmalloc(pool, sizeof(*data));
		if (WARN(!data, "Failed to allocate prlist node"))
			goto out;
		wr_int(&data->d_int, i);
		wr_ulonglong(&data->d_ulonglong, i);
		prlist_add_tail(&data->node, &test_prlist_head);
	}
	for (i = 1; i < LIST_NODES; i++) {
		data = (struct prlist_data *)pmalloc(pool, sizeof(*data));
		if (WARN(!data, "Failed to allocate prlist node"))
			goto out;
		wr_int(&data->d_int, i);
		wr_ulonglong(&data->d_ulonglong, i);
		prlist_add(&data->node, &test_prlist_head);
	}
	i = LIST_NODES;
	delta = -1;
	list_for_each_entry(data, &test_prlist_head, node) {
		i += delta;
		if (!i)
			delta = 1;
		if (WARN(data->d_int != i || data->d_ulonglong != i,
			 "unexpected value in prlist, build test failed"))
			goto out;
	}
	pr_info("build prlist test passed");
	return true;
out:
	pmalloc_destroy_pool(pool);
	return false;
}

static bool test_teardown_prlist(void)
{
	short i;

	for (i = 0; !list_empty(&test_prlist_head.list); i++)
		prlist_del_entry(test_prlist_head.next);
	if (WARN(i != LIST_NODES * 2 - 1, "teardown prlist test failed"))
		return false;
	pmalloc_destroy_pool(pool);
	pr_info("teardown prlist test passed");
	return true;
}

static bool test_prlist(void)
{
	if (WARN(!(test_init_prlist_head() &&
		   test_build_prlist() &&
		   test_teardown_prlist()),
		 "prlist test failed"))
		return false;
	pr_info("prlist test passed");
	return true;
}

/* ---------------------- prhlist test functions ---------------------- */
static struct prhlist_head test_prhlist_head __wr_after_init =
	PRHLIST_HEAD_INIT;

static bool test_init_prhlist_head(void)
{
	if (WARN(test_prhlist_head.first,
		 "static initialization of static prhlist_head failed"))
		return false;
	wr_ptr(&test_prhlist_head.first, (void *)-1);
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

struct prhlist_data {
	int d_int;
	struct prhlist_node node;
	unsigned long long d_ulonglong;
};

static bool test_build_prhlist(void)
{
	short i;
	struct prhlist_data *data;
	struct prhlist_node *anchor;

	pool = prhlist_create_pool();
	if (WARN(!pool, "could not create pool"))
		return false;

	for (i = 2 * LIST_INTERVAL - 1; i >= LIST_INTERVAL; i--) {
		data = (struct prhlist_data *)pmalloc(pool, sizeof(*data));
		if (WARN(!data, "Failed to allocate prhlist node"))
			goto out;
		wr_int(&data->d_int, i);
		wr_ulonglong(&data->d_ulonglong, i);
		prhlist_add_head(&data->node, &test_prhlist_head);
	}
	anchor = test_prhlist_head.first;
	for (i = 0; i < LIST_INTERVAL; i++) {
		data = (struct prhlist_data *)pmalloc(pool, sizeof(*data));
		if (WARN(!data, "Failed to allocate prhlist node"))
			goto out;
		wr_int(&data->d_int, i);
		wr_ulonglong(&data->d_ulonglong, i);
		prhlist_add_before(&data->node, anchor);
	}
	hlist_for_each_entry(data, &test_prhlist_head, node)
		if (!data->node.next)
			anchor = &data->node;
	for (i = 3 * LIST_INTERVAL - 1; i >= 2 * LIST_INTERVAL; i--) {
		data = (struct prhlist_data *)pmalloc(pool, sizeof(*data));
		if (WARN(!data, "Failed to allocate prhlist node"))
			goto out;
		wr_int(&data->d_int, i);
		wr_ulonglong(&data->d_ulonglong, i);
		prhlist_add_behind(&data->node, anchor);
	}
	i = 0;
	hlist_for_each_entry(data, &test_prhlist_head, node) {
		if (WARN(data->d_int != i || data->d_ulonglong != i,
			 "unexpected value in prhlist, build test failed"))
			goto out;
		i++;
	}
	if (WARN(i != LIST_NODES,
		 "wrong number of nodes: %d, expectd %d", i, LIST_NODES))
		goto out;
	pr_info("build prhlist test passed");
	return true;
out:
	pmalloc_destroy_pool(pool);
	return false;
}

static bool test_teardown_prhlist(void)
{
	struct prhlist_node **pnode;
	bool retval = false;

	for (pnode = &test_prhlist_head.first->next; *pnode;) {
		if (WARN(*(*pnode)->pprev != *pnode,
			 "inconsistent pprev value, delete test failed"))
			goto err;
		prhlist_del(*pnode);
	}
	prhlist_del(test_prhlist_head.first);
	if (WARN(!hlist_empty(&test_prhlist_head.head),
		 "prhlist is not empty, delete test failed"))
		goto err;
	pr_info("deletion of prhlist passed");
	retval = true;
err:
	pmalloc_destroy_pool(pool);
	return retval;
}

static bool test_prhlist(void)
{
	if (WARN(!(test_init_prhlist_head() &&
		   test_build_prhlist() &&
		   test_teardown_prhlist()),
		 "prhlist test failed"))
		return false;
	pr_info("prhlist test passed");
	return true;
}

/* -------------------- prlist_rcu test functions -------------------- */
static bool test_prlist_rcu(void)
{
	return true;
}

static int __init test_prlists_init_module(void)
{
	if (WARN(!(test_prlist() &&
		   test_prhlist() &&
		   test_prlist_rcu()),
		 "protected lists test failed"))
		return -EFAULT;
	pr_info("protected lists test passed");
	return 0;
}

module_init(test_prlists_init_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Igor Stoppa <igor.stoppa@huawei.com>");
MODULE_DESCRIPTION("Test module for protected doubly linked list.");
