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

//#define PROT_LIST_AUTO_RW 0
//#define PROT_LIST_AUTO_RO 1

#define PROT_LIST_AUTO_RW (PMALLOC_RW | PMALLOC_START_RW | PMALLOC_SHIFT_RW_RO)
#define PROT_LIST_AUTO_RO (PMALLOC_RW | PMALLOC_START_RW | PMALLOC_SHIFT_RO)


struct prot_list_pool {
	struct pmalloc_pool pool;
};

struct prot_head {
	struct prot_head *next, *prev;
};

struct prot_list_pool *prot_list_create_custom_pool(size_t refill,
						    unsigned short align_order,
						    bool auto_ro);

static inline struct prot_list_pool *prot_list_create_pool(void)
{
	return prot_list_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					    PMALLOC_ALIGN_DEFAULT, 0);
}

static inline void INIT_PROT_LIST_HEAD(struct prot_list_pool *pool,
				       struct prot_head *list)
{
	struct prot_head head = {list, list};

	pmalloc_rare_write(&pool->pool, list, &head, sizeof(struct prot_head));
}

static inline struct prot_head *PROT_LIST_HEAD(struct prot_list_pool *pool)
{
	struct prot_head *head;

	head = pmalloc(&pool->pool, sizeof(struct prot_head));
	if (WARN(!head, "Could not allocate protected list head."))
		return NULL;
	INIT_PROT_LIST_HEAD(pool, head);
	return head;

}
#endif
