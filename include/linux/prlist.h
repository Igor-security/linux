/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protected Doubly Linked List
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Code from <linux/list.h> adapted to perform writes on rare-write data.
 */

#ifndef _LINUX_PRLIST_H
#define _LINUX_PRLIST_H

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/pmalloc.h>

/*
 * Circular Protected Doubly Linked List
 */
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

#define PRLIST_HEAD_INIT(name)	{{.list = LIST_HEAD_INIT(name)}}

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
void prlist_set_prev(struct pmalloc_pool *pool, struct prlist_head *head,
		     const struct prlist_head *prev)
{
	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(&head->prev, prev);
	else
		pmalloc_rare_write_ptr(pool, &head->prev, prev);
}

static __always_inline
void prlist_set_next(struct pmalloc_pool *pool, struct prlist_head *head,
		     const struct prlist_head *next)
{
	if (unlikely(rare_write_check_boundaries(head, sizeof(head))))
		rare_write_ptr(&head->next, next);
	else
		pmalloc_rare_write_ptr(pool, &head->next, next);
}

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
	prlist_set_next(pool, new, head->next);
	prlist_set_prev(pool, new, head);
	prlist_set_prev(pool, head->next, new);
	prlist_set_next(pool, head, new);
}

static __always_inline
void prlist_add_tail(struct pmalloc_pool *pool, struct prlist_head *new,
		     struct prlist_head *head)
{
	prlist_set_next(pool, new, head);
	prlist_set_prev(pool, new, head->prev);
	prlist_set_next(pool, head->prev, new);
	prlist_set_prev(pool, head, new);
}

static __always_inline
void prlist_del_entry(struct pmalloc_pool *pool, struct prlist_head *entry)
{
	prlist_set_prev(pool, entry->next, entry->prev);
	prlist_set_next(pool, entry->prev, entry->next);
	pmalloc_rare_write_ptr(pool, &entry->next, LIST_POISON1);
	pmalloc_rare_write_ptr(pool, &entry->prev, LIST_POISON2);
}

static __always_inline void dump_prlist_head(struct prlist_head *head)
{
	pr_info("head: 0x%08lx   prev: 0x%08lx   next: 0x%08lx",
		(unsigned long)head, (unsigned long)head->prev,
		(unsigned long)head->next);
}

/*
 * Protected Doubly Linked List with single pointer list head
 */
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
		struct hlist_node node;;
	};
};

#define PRHLIST_HEAD_INIT	{{.head = HLIST_HEAD_INIT}}

#define is_static(object) \
	unlikely(rare_write_check_boundaries(object, sizeof(*object)))

static __always_inline
struct pmalloc_pool *prhlist_create_custom_pool(size_t refill,
						unsigned short align_order)
{
	return pmalloc_create_custom_pool(refill, align_order,
					  PMALLOC_AUTO_RW);
}

static __always_inline
struct pmalloc_pool *prhlist_create_pool(void)
{
	return prhlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					  PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void prhlist_set_first(struct pmalloc_pool *pool, struct prhlist_head *head,
		       struct prhlist_node *first)
{
	if (is_static(head))
		rare_write_ptr(&head->first, first);
	else
		pmalloc_rare_write_ptr(pool, &head->first, first);
}

static __always_inline
void prhlist_set_next(struct pmalloc_pool *pool, struct prhlist_node *node,
		      struct prhlist_node *next)
{
	if (is_static(node))
		rare_write_ptr(&node->next, next);
	else
		pmalloc_rare_write_ptr(pool, &node->next, next);
}

static __always_inline
void prhlist_set_pprev(struct pmalloc_pool *pool, struct prhlist_node *node,
		       struct prhlist_node **pprev)
{
	if (is_static(node))
		rare_write_ptr(&node->pprev, pprev);
	else
		pmalloc_rare_write_ptr(pool, &node->pprev, pprev);
}

static __always_inline
void prhlist_set_prev(struct pmalloc_pool *pool, struct prhlist_node *node,
		       struct prhlist_node *prev)
{
	if (is_static(node->pprev))
		rare_write_ptr(node->pprev, prev);
	else
		pmalloc_rare_write_ptr(pool, node->pprev, prev);
}

static __always_inline
void INIT_PRHLIST_HEAD(struct pmalloc_pool *pool, struct prhlist_head *head)
{
	prhlist_set_first(pool, head, NULL);
}

static __always_inline
void INIT_STATIC_PRHLIST_HEAD(struct prhlist_head *head)
{
	INIT_PRHLIST_HEAD(NULL, head);
}

static __always_inline
void INIT_PRHLIST_NODE(struct pmalloc_pool *pool, struct prhlist_node *node)
{
	pmalloc_rare_write_ptr(pool, &node->next, NULL);
	pmalloc_rare_write_ptr(pool, &node->pprev, NULL);
}

static __always_inline
void prhlist_add_head(struct pmalloc_pool *pool, struct prhlist_node *node,
		      struct prhlist_head *head)
{
	struct prhlist_node *first;

	first = head->first;
	prhlist_set_next(pool, node, first);
	if (first)
		prhlist_set_pprev(pool, first, &node->next);
	prhlist_set_first(pool, head, node);
	prhlist_set_pprev(pool, node, &head->first);
}

/* next must be != NULL */
static __always_inline
void prhlist_add_before(struct pmalloc_pool *pool, struct prhlist_node *node,
			struct prhlist_node *next)
{
	prhlist_set_pprev(pool, node, next->pprev);
	prhlist_set_next(pool, node, next);
	prhlist_set_pprev(pool, next, &node->next);
	prhlist_set_prev(pool, node, node);
}

static __always_inline
void prhlist_add_behind(struct pmalloc_pool *pool, struct prhlist_node *node,
			struct prhlist_node *prev)
{
	prhlist_set_next(pool, node, prev->next);
	prhlist_set_next(pool, prev, node);
	prhlist_set_pprev(pool, node, &prev->next);
	if (node->next)
		prhlist_set_pprev(pool, node->next, &node->next);
}

/* after that we'll appear to be on some hlist and hlist_del will work */
static __always_inline
void prhlist_add_fake(struct pmalloc_pool *pool, struct prhlist_node *node)
{
	prhlist_set_pprev(pool, node, &node->next);
}

static __always_inline
void __prhlist_del(struct pmalloc_pool *pool, struct prhlist_node *node)
{
	struct prhlist_node *next;
	struct prhlist_node *prev;
	struct prhlist_node **pprev;

	next = node->next;
	pprev = node->pprev;
	prev = container_of(pprev, struct prhlist_node, next);
	prhlist_set_next(pool, prev, next);
	if (next)
		prhlist_set_pprev(pool, next, pprev);
}

static __always_inline
void prhlist_del(struct pmalloc_pool *pool, struct prhlist_node *node)
{
	__prhlist_del(pool, node);
	pmalloc_rare_write_ptr(pool, &node->next, LIST_POISON1);
	pmalloc_rare_write_ptr(pool, &node->pprev, LIST_POISON2);
}

static __always_inline
void prhlist_del_init(struct pmalloc_pool *pool, struct prhlist_node *node)
{
	if (!hlist_unhashed(&node->node)) {
		__prhlist_del(pool, node);
		INIT_PRHLIST_NODE(pool, node);
	}
}

#endif
