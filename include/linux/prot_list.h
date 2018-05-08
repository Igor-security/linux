/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prot_list.h: Header for Protectable Double Linked List
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PROT_LIST_H
#define _LINUX_PROT_LIST_H

#include <linux/pmalloc.h>
#include <linux/list.h>
#include <linux/kernel.h>

struct prot_head {
	struct prot_head *next, *prev;
};

static inline void INIT_PROT_LIST_HEAD(struct pmalloc_pool *pool,
				       struct prot_head *list)
{
	struct prot_head head = {list, list};

	pmalloc_rare_write(pool, list, &head, sizeof(struct prot_head));
}
#endif
