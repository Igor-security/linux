// SPDX-License-Identifier: GPL-2.0
/*
 * prlist.c: Protected Double Linked List
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/prlist.h>

struct prlist_pool *prlist_create_custom_pool(size_t refill,
					      unsigned short align_order)
{
	struct prlist_pool *pool;

	pool = kzalloc(sizeof(struct prlist_pool), GFP_KERNEL);
	if (WARN(!pool, "Could not allocate pool meta data."))
		return NULL;
	pmalloc_init_custom_pool(&pool->pool, refill, align_order,
				 PMALLOC_AUTO_RW);
	return pool;
}
EXPORT_SYMBOL(prlist_create_custom_pool);
