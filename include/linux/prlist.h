/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protected Double Linked List
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PRLIST_H
#define _LINUX_PRLIST_H

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/pmalloc.h>

struct prlist_pool {
	struct pmalloc_pool pool;
};

struct prlist_head {
	struct list_head list;
};

static __always_inline
struct prlist_head *list_to_prlist(struct list_head *list)
{
	return container_of(list, struct prlist_head, list);
}

struct prlist_pool
*prlist_create_custom_pool(size_t refill, unsigned short align_order);

static __always_inline
struct prlist_pool *prlist_create_pool(void)
{
	return prlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					 PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void *prlist_alloc(struct prlist_pool *pool, size_t size)
{
	return pmalloc(&pool->pool, size);
}

static __always_inline void prlist_set_prev(struct prlist_pool *pool,
					    struct prlist_head *head,
					    const struct prlist_head *prev)
{
	void *dst = &head->list.prev;
	const void *src = &prev->list;

	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(dst, src);
	else
		pmalloc_rare_write_ptr(&pool->pool, dst, src);
}

static __always_inline void prlist_set_next(struct prlist_pool *pool,
					    struct prlist_head *head,
					    const struct prlist_head *next)
{
	void *dst = &head->list.next;
	const void *src = &next->list;

	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(dst, src);
	else
		pmalloc_rare_write_ptr(&pool->pool, dst, src);
}

static __always_inline
void INIT_PRLIST_HEAD(struct prlist_pool *pool, struct prlist_head *head)
{
	prlist_set_prev(pool, head, head);
	prlist_set_next(pool, head, head);
}

static __always_inline
void INIT_STATIC_PRLIST_HEAD(struct prlist_head *head)
{
	INIT_PRLIST_HEAD(NULL, head);
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
