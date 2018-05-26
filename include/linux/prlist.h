/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protectable Double Linked List
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PRLIST_H
#define _LINUX_PRLIST_H

#include <linux/pmalloc.h>
#include <linux/list.h>
#include <linux/kernel.h>

struct prlist_pool {
	struct pmalloc_pool pool;
};

struct prlist_head {
	struct list_head list;
};

static __always_inline
struct prhead *list_to_prlist(struct list_head *list)
{
return NULL;
	//	return container_of(list, struct prhead, list);
}

struct prlist_pool
*prlist_create_custom_pool(size_t refill, unsigned short align_order);

static inline
struct prlist_pool *prlist_create_pool(void)
{
	return prlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					 PMALLOC_ALIGN_ORDER_DEFAULT);
}

static inline void *prlist_alloc(struct prlist_pool *pool, size_t size)
{
	return pmalloc(&pool->pool, size);
}

static inline void INIT_PRLIST_HEAD(struct prlist_pool *pool,
				    struct prlist_head *head)
{
	void *pippo = &head->list;
	pmalloc_rare_write(&pool->pool, &head->list.next, &pippo,
			   sizeof(&head->list.next));
}
/*
static __always_inline
void prlist_set_next(struct prlist_head *head, struct list_head *next)
{
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

static inline
void prot_list_add(struct list_head *new, struct list_head *head)
{
	__prot_list_add();
}

static inline
void prot_list_add_tail(struct list_head *new, struct list_head *head)
{
}
*/
#endif
