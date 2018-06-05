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

struct prlist_head {
	union {
		struct list_head list;
		struct {
			struct list_head *next, *prev;
		};
		struct {
			struct prlist_head *prnext, *prprev;
		};
	};
};

struct prhlist_node {
	struct hlist_node node;
};

struct prhlist_head {
	union {
		struct hlist_head head;
		//struct prhlist_head prhead;
	};
};

static __always_inline
struct prlist_head *list_to_prlist(struct list_head *list)
{
	return container_of(list, struct prlist_head, list);
}

static __always_inline
struct pmalloc_pool *prlist_create_custom_pool(size_t refill,
					       unsigned short align_order)
{
	return pmalloc_create_custom_pool(refill, align_order,
					  PMALLOC_AUTO_RW);
}

static __always_inline
struct pmalloc_pool *prlist_create_pool(void)
{
	return prlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					 PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void *prlist_alloc(struct pmalloc_pool *pool, size_t size)
{
	return pmalloc(pool, size);
}

static __always_inline
void prlist_set_prev(struct pmalloc_pool *pool, struct prlist_head *head,
		     const struct prlist_head *prev)
{
	void *dst = &head->prev;

	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(dst, prev);
	else
		pmalloc_rare_write_ptr(pool, dst, prev);
}

static __always_inline
void prlist_set_next(struct pmalloc_pool *pool, struct prlist_head *head,
		     const struct prlist_head *next)
{
	void *dst = &head->next;

	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(dst, next);
	else
		pmalloc_rare_write_ptr(pool, dst, next);
}

#define PRLIST_HEAD(head) {{.prprev = head, .prnext = head,}}

static __always_inline
void INIT_PRLIST_HEAD(struct pmalloc_pool *pool, struct prlist_head *head)
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
void prlist_add(struct pmalloc_pool *pool, struct prlist_head *new,
		struct prlist_head *head)
{
	prlist_set_next(pool, new, head->prnext);
	prlist_set_prev(pool, new, head);
	prlist_set_prev(pool, head->prnext, new);
	prlist_set_next(pool, head, new);
}

static __always_inline
void prlist_add_tail(struct pmalloc_pool *pool, struct prlist_head *new,
		     struct prlist_head *head)
{
	prlist_set_next(pool, new, head);
	prlist_set_prev(pool, new, head->prprev);
	prlist_set_next(pool, head->prprev, new);
	prlist_set_prev(pool, head, new);
}

static __always_inline
void prlist_del_entry(struct pmalloc_pool *pool, struct prlist_head *entry)
{
	prlist_set_prev(pool, entry->prnext, entry->prprev);
	prlist_set_next(pool, entry->prprev, entry->prnext);
	pmalloc_rare_write_ptr(pool, &entry->next, LIST_POISON1);
	pmalloc_rare_write_ptr(pool, &entry->prev, LIST_POISON2);
}

static __always_inline void dump_prlist_head(struct prlist_head *head)
{
	pr_info("head: 0x%08lx   prev: 0x%08lx   next: 0x%08lx",
		(unsigned long)head, (unsigned long)head->prev,
		(unsigned long)head->next);
}

static __always_inline void prlist_destroy_pool(struct pmalloc_pool *pool)
{
	pmalloc_destroy_pool(pool);
}

#define PRHLIST_HEAD_INIT {.head = HLIST_HEAD_INIT}

#endif
