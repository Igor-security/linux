// SPDX-License-Identifier: GPL-2.0
/*
 * prot_list.c: protected double linked list
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/prot_list.h>

struct prot_list_pool *prot_list_create_custom_pool(size_t refill,
						    unsigned short align_order)
{
	struct prot_list_pool *pool;

	pool = kzalloc(sizeof(struct prot_list_pool), GFP_KERNEL);
	if (WARN(!pool, "Could not allocate pool meta data."))
		return NULL;
	pmalloc_init_custom_pool(&pool->pool, refill, align_order,
				 PMALLOC_AUTO_RW);
	return pool;
}
EXPORT_SYMBOL(prot_list_create_custom_pool);
