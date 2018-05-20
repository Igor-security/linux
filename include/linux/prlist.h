/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protected Lists
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Code from <linux/list.h> and <linux/rculist.h>, adapted to perform
 * writes on write-rare data.
 */

#ifndef _LINUX_PRLIST_H
#define _LINUX_PRLIST_H

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/prmemextra.h>

/* --------------- Circular Protected Doubly Linked List --------------- */
struct prlist_head {
	union {
		struct list_head list;
		struct {
			struct prlist_head *next, *prev;
		};
	};
};

static __always_inline
struct prlist_head *to_prlist_head(struct list_head *list)
{
	return container_of(list, struct prlist_head, list);
}

#define PRLIST_HEAD_INIT(name) {		\
	{					\
		.list = LIST_HEAD_INIT(name),	\
	}					\
}

static __always_inline
struct pmalloc_pool *prlist_create_custom_pool(size_t refill,
					       unsigned short align_order)
{
	return pmalloc_create_custom_pool(refill, align_order,
					  PMALLOC_START_WR);
}

static __always_inline
struct pmalloc_pool *prlist_create_pool(void)
{
	return prlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					 PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void prlist_set_prev(struct prlist_head *head,
		     const struct prlist_head *prev)
{
	wr_ptr(&head->prev, prev);
}

static __always_inline
void prlist_set_next(struct prlist_head *head,
		     const struct prlist_head *next)
{
	wr_ptr(&head->next, next);
}

static __always_inline
void INIT_PRLIST_HEAD(struct prlist_head *head)
{
	prlist_set_prev(head, head);
	prlist_set_next(head, head);
}

static __always_inline
void INIT_STATIC_PRLIST_HEAD(struct prlist_head *head)
{
	INIT_PRLIST_HEAD(head);
}

static __always_inline
void prlist_add(struct prlist_head *new, struct prlist_head *head)
{
	prlist_set_next(new, head->next);
	prlist_set_prev(new, head);
	prlist_set_prev(head->next, new);
	prlist_set_next(head, new);
}

static __always_inline
void prlist_add_tail(struct prlist_head *new, struct prlist_head *head)
{
	prlist_set_next(new, head);
	prlist_set_prev(new, head->prev);
	prlist_set_next(head->prev, new);
	prlist_set_prev(head, new);
}

static __always_inline
void prlist_del_entry(struct prlist_head *entry)
{
	prlist_set_prev(entry->next, entry->prev);
	prlist_set_next(entry->prev, entry->next);
	wr_ptr(&entry->next, LIST_POISON1);
	wr_ptr(&entry->prev, LIST_POISON2);
}

static __always_inline void dump_prlist_head(struct prlist_head *head)
{
	pr_info("head: 0x%08lx   prev: 0x%08lx   next: 0x%08lx",
		(unsigned long)head, (unsigned long)head->prev,
		(unsigned long)head->next);
}

/* ---- Protected Doubly Linked List with single pointer list head ---- */
struct prhlist_head {
	union {
		struct prhlist_node *first;
		struct hlist_head head;
	};
};

struct prhlist_node {
	union{
		struct {
			struct prhlist_node *next, **pprev;
		};
		struct hlist_node node;
	};
};

#define PRHLIST_HEAD_INIT	{		\
	{					\
		.head = HLIST_HEAD_INIT,	\
	}					\
}

#define is_static(object) \
	unlikely(wr_check_boundaries(object, sizeof(*object)))

static __always_inline
struct pmalloc_pool *prhlist_create_custom_pool(size_t refill,
						unsigned short align_order)
{
	return pmalloc_create_custom_pool(refill, align_order,
					  PMALLOC_AUTO_WR);
}

static __always_inline
struct pmalloc_pool *prhlist_create_pool(void)
{
	return prhlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					  PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void prhlist_set_first(struct prhlist_head *head,
		       struct prhlist_node *first)
{
	wr_ptr(&head->first, first);
}

static __always_inline
void prhlist_set_next(struct prhlist_node *node, struct prhlist_node *next)
{
	wr_ptr(&node->next, next);
}

static __always_inline
void prhlist_set_pprev(struct prhlist_node *node,
		       struct prhlist_node **pprev)
{
	wr_ptr(&node->pprev, pprev);
}

static __always_inline
void prhlist_set_prev(struct prhlist_node *node,
		       struct prhlist_node *prev)
{
	wr_ptr(node->pprev, prev);
}

static __always_inline
void INIT_PRHLIST_HEAD(struct prhlist_head *head)
{
	prhlist_set_first(head, NULL);
}

static __always_inline
void INIT_STATIC_PRHLIST_HEAD(struct prhlist_head *head)
{
	INIT_PRHLIST_HEAD(head);
}

static __always_inline
void INIT_PRHLIST_NODE(struct prhlist_node *node)
{
	wr_ptr(&node->next, NULL);
	wr_ptr(&node->pprev, NULL);
}

static __always_inline
void prhlist_add_head(struct prhlist_node *node,
		      struct prhlist_head *head)
{
	struct prhlist_node *first;

	first = head->first;
	prhlist_set_next(node, first);
	if (first)
		prhlist_set_pprev(first, &node->next);
	prhlist_set_first(head, node);
	prhlist_set_pprev(node, &head->first);
}

/* next must be != NULL */
static __always_inline
void prhlist_add_before(struct prhlist_node *node,
			struct prhlist_node *next)
{
	prhlist_set_pprev(node, next->pprev);
	prhlist_set_next(node, next);
	prhlist_set_pprev(next, &node->next);
	prhlist_set_prev(node, node);
}

static __always_inline
void prhlist_add_behind(struct prhlist_node *node,
			struct prhlist_node *prev)
{
	prhlist_set_next(node, prev->next);
	prhlist_set_next(prev, node);
	prhlist_set_pprev(node, &prev->next);
	if (node->next)
		prhlist_set_pprev(node->next, &node->next);
}

/* after that we'll appear to be on some hlist and hlist_del will work */
static __always_inline
void prhlist_add_fake(struct prhlist_node *node)
{
	prhlist_set_pprev(node, &node->next);
}

static __always_inline
void __prhlist_del(struct prhlist_node *node)
{
	struct prhlist_node *next;
	struct prhlist_node *prev;
	struct prhlist_node **pprev;

	next = node->next;
	pprev = node->pprev;
	prev = container_of(pprev, struct prhlist_node, next);
	prhlist_set_next(prev, next);
	if (next)
		prhlist_set_pprev(next, pprev);
}

static __always_inline
void prhlist_del(struct prhlist_node *node)
{
	__prhlist_del(node);
	wr_ptr(&node->next, LIST_POISON1);
	wr_ptr(&node->pprev, LIST_POISON2);
}

static __always_inline
void prhlist_del_init(struct prhlist_node *node)
{
	if (!hlist_unhashed(&node->node)) {
		__prhlist_del(node);
		INIT_PRHLIST_NODE(node);
	}
}

/* ------------- Circular Protected Doubly Linked List RCU ------------- */

#endif
