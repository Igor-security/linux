/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protected Doubly Linked List
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

static __always_inline
struct prlist_head *prlist_prev(struct prlist_head *head)
{
	return list_to_prlist(head->list.prev);
}

static __always_inline
struct prlist_head *prlist_next(struct prlist_head *head)
{
	return list_to_prlist(head->list.next);
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

static __always_inline
void prlist_set_prev(struct prlist_pool *pool, struct prlist_head *head,
		     const struct prlist_head *prev)
{
	void *dst = &head->list.prev;
	const void *src = &prev->list;

	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(dst, src);
	else
		pmalloc_rare_write_ptr(&pool->pool, dst, src);
}

static __always_inline
void prlist_set_next(struct prlist_pool *pool, struct prlist_head *head,
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

static __always_inline
void prlist_add(struct prlist_pool *pool, struct prlist_head *new,
		struct prlist_head *head)
{
	struct prlist_head *next = list_to_prlist(head->list.next);

	prlist_set_next(pool, new, next);
	prlist_set_prev(pool, new, head);

	prlist_set_prev(pool, next, new);
	prlist_set_next(pool, head, new);
}

static __always_inline
void prlist_add_tail(struct prlist_pool *pool, struct prlist_head *new,
		     struct prlist_head *head)
{
	struct prlist_head *prev = list_to_prlist(head->list.prev);

	prlist_set_next(pool, new, head);
	prlist_set_prev(pool, new, prev);

	prlist_set_prev(pool, head, new);
	prlist_set_next(pool, prev, new);
}

static __always_inline
void prlist_del_entry(struct prlist_pool *pool, struct prlist_head *entry)
{
	struct prlist_head *next;
	struct prlist_head *prev;

	next = list_to_prlist(entry->list.next);
	prev = list_to_prlist(entry->list.prev);
	prlist_set_prev(pool, next, prev);
	prlist_set_next(pool, prev, next);
	pmalloc_rare_write_ptr(&pool->pool, &entry->list.next, LIST_POISON1);
	pmalloc_rare_write_ptr(&pool->pool, &entry->list.prev, LIST_POISON2);
}

static __always_inline void dump_prlist_head(struct prlist_head *head)
{
	pr_info("head: 0x%08lx   prev: 0x%08lx   next: 0x%08lx",
		(unsigned long)head, (unsigned long)head->list.prev,
		(unsigned long)head->list.next);
}

static inline void prlist_destroy_pool(struct prlist_pool *pool)
{
	pmalloc_destroy_pool(&pool->pool);
}

#endif
