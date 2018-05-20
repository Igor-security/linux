/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prlist.h: Header for Protected Lists
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Code from <linux/list.h> and <linux/rculist.h>, adapted to perform
 * writes on write-rare data.
 * These functions and macros rely on data structures that allow the reuse
 * of what is already provided for reading the content of their non-write
 * rare variant.
 */

#ifndef _LINUX_PRLIST_H
#define _LINUX_PRLIST_H

#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/prmemextra.h>

/* --------------- Circular Protected Doubly Linked List --------------- */
union prlist_head {
	struct list_head list;
	struct {
		union prlist_head *next;
		union prlist_head *prev;
	};
} __attribute__((aligned(sizeof(void *))));

static __always_inline
union prlist_head *to_prlist_head(struct list_head *list)
{
	return container_of(list, union prlist_head, list);
}

#define PRLIST_HEAD_INIT(name) {	\
	.list = LIST_HEAD_INIT(name),	\
}

static __always_inline
struct pmalloc_pool *prlist_create_custom_pool(size_t refill,
					       unsigned short align_order)
{
	
	return pmalloc_create_custom_pool(refill, align_order,
					  PMALLOC_START_WR);
}

static __always_inline struct pmalloc_pool *prlist_create_pool(void)
{
	return prlist_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					 PMALLOC_ALIGN_ORDER_DEFAULT);
}

static __always_inline
void prlist_set_prev(union prlist_head *head,
		     const union prlist_head *prev)
{
	wr_ptr(&head->prev, prev);
}

static __always_inline
void prlist_set_next(union prlist_head *head,
		     const union prlist_head *next)
{
	wr_ptr(&head->next, next);
}

static __always_inline void INIT_PRLIST_HEAD(union prlist_head *head)
{
	prlist_set_prev(head, head);
	prlist_set_next(head, head);
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __always_inline
void __prlist_add(union prlist_head *new, union prlist_head *prev,
		  union prlist_head *next)
{
	if (!__list_add_valid(&new->list, &prev->list, &next->list))
		return;

	prlist_set_prev(next, new);
	prlist_set_next(new, next);
	prlist_set_prev(new, prev);
	prlist_set_next(prev, new);
}

/**
 * prlist_add - add a new entry
 * @new: new entry to be added
 * @head: prlist head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static __always_inline
void prlist_add(union prlist_head *new, union prlist_head *head)
{
	__prlist_add(new, head, head->next);
}

/**
 * prlist_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static __always_inline
void prlist_add_tail(union prlist_head *new, union prlist_head *head)
{
	__prlist_add(new, head->prev, head);
}

/*
 * Delete a prlist entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __always_inline
void __prlist_del(union prlist_head * prev, union prlist_head * next)
{
	prlist_set_prev(next, prev);
	prlist_set_next(prev, next);
}

/**
 * prlist_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void __prlist_del_entry(union prlist_head *entry)
{
	if (!__list_del_entry_valid(&entry->list))
		return;
	__prlist_del(entry->prev, entry->next);
}

static __always_inline void prlist_del(union prlist_head *entry)
{
	__prlist_del_entry(entry);
	prlist_set_next(entry, LIST_POISON1);
	prlist_set_prev(entry, LIST_POISON2);
}

/**
 * prlist_replace - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * If @old was empty, it will be overwritten.
 */
static __always_inline
void prlist_replace(union prlist_head *old, union prlist_head *new)
{
	prlist_set_next(new, old->next);
	prlist_set_prev(new->next, new);
	prlist_set_prev(new, old->prev);
	prlist_set_next(new->prev, new);
}

static __always_inline
void prlist_replace_init(union prlist_head *old, union prlist_head *new)
{
	prlist_replace(old, new);
	INIT_PRLIST_HEAD(old);
}

/**
 * prlist_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
static __always_inline void prlist_del_init(union prlist_head *entry)
{
	__prlist_del_entry(entry);
	INIT_PRLIST_HEAD(entry);
}

/**
 * prlist_move - delete from one list and add as another's head
 * @list: the entry to move
 * @head: the head that will precede our entry
 */
static __always_inline
void prlist_move(union prlist_head *list, union prlist_head *head)
{
	__prlist_del_entry(list);
	prlist_add(list, head);
}

/**
 * prlist_move_tail - delete from one list and add as another's tail
 * @list: the entry to move
 * @head: the head that will follow our entry
 */
static __always_inline
void prlist_move_tail(union prlist_head *list, union prlist_head *head)
{
	__prlist_del_entry(list);
	prlist_add_tail(list, head);
}

/**
 * prlist_rotate_left - rotate the list to the left
 * @head: the head of the list
 */
static __always_inline void prlist_rotate_left(union prlist_head *head)
{
	union prlist_head *first;

	if (!list_empty(&head->list)) {
		first = head->next;
		prlist_move_tail(first, head);
	}
}

static __always_inline
void __prlist_cut_position(union prlist_head *list, union prlist_head *head,
			   union prlist_head *entry)
{
	union prlist_head *new_first = entry->next;

	prlist_set_next(list, head->next);
	prlist_set_prev(list->next, list);
	prlist_set_prev(list, entry);
	prlist_set_next(entry, list);
	prlist_set_next(head, new_first);
	prlist_set_prev(new_first, head);
}

/**
 * prlist_cut_position - cut a list into two
 * @list: a new list to add all removed entries
 * @head: a list with entries
 * @entry: an entry within head, could be the head itself
 *	and if so we won't cut the list
 *
 * This helper moves the initial part of @head, up to and
 * including @entry, from @head to @list. You should
 * pass on @entry an element you know is on @head. @list
 * should be an empty list or a list you do not care about
 * losing its data.
 *
 */
static __always_inline
void prlist_cut_position(union prlist_head *list, union prlist_head *head,
			 union prlist_head *entry)
{
	if (list_empty(&head->list))
		return;
	if (list_is_singular(&head->list) &&
		(head->next != entry && head != entry))
		return;
	if (entry == head)
		INIT_PRLIST_HEAD(list);
	else
		__prlist_cut_position(list, head, entry);
}

/**
 * prlist_cut_before - cut a list into two, before given entry
 * @list: a new list to add all removed entries
 * @head: a list with entries
 * @entry: an entry within head, could be the head itself
 *
 * This helper moves the initial part of @head, up to but
 * excluding @entry, from @head to @list.  You should pass
 * in @entry an element you know is on @head.  @list should
 * be an empty list or a list you do not care about losing
 * its data.
 * If @entry == @head, all entries on @head are moved to
 * @list.
 */
static __always_inline
void prlist_cut_before(union prlist_head *list, union prlist_head *head,
		       union prlist_head *entry)
{
	if (head->next == entry) {
		INIT_PRLIST_HEAD(list);
		return;
	}
	prlist_set_next(list, head->next);
	prlist_set_prev(list->next, list);
	prlist_set_prev(list, entry->prev);
	prlist_set_next(list->prev, list);
	prlist_set_next(head, entry);
	prlist_set_prev(entry, head);
}

static __always_inline
void __prlist_splice(const union prlist_head *list, union prlist_head *prev,
				 union prlist_head *next)
{
	union prlist_head *first = list->next;
	union prlist_head *last = list->prev;

	prlist_set_prev(first, prev);
	prlist_set_next(prev, first);
	prlist_set_next(last, next);
	prlist_set_prev(next, last);
}

/**
 * prlist_splice - join two lists, this is designed for stacks
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static __always_inline
void prlist_splice(const union prlist_head *list, union prlist_head *head)
{
	if (!list_empty(&list->list))
		__prlist_splice(list, head, head->next);
}

/**
 * prlist_splice_tail - join two lists, each list being a queue
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static __always_inline
void prlist_splice_tail(union prlist_head *list, union prlist_head *head)
{
	if (!list_empty(&list->list))
		__prlist_splice(list, head->prev, head);
}

/**
 * prlist_splice_init - join two lists and reinitialise the emptied list.
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * The list at @list is reinitialised
 */
static __always_inline
void prlist_splice_init(union prlist_head *list, union prlist_head *head)
{
	if (!list_empty(&list->list)) {
		__prlist_splice(list, head, head->next);
		INIT_PRLIST_HEAD(list);
	}
}

/**
 * prlist_splice_tail_init - join 2 lists and reinitialise the emptied list
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * Each of the lists is a queue.
 * The list at @list is reinitialised
 */
static __always_inline
void prlist_splice_tail_init(union prlist_head *list,
			     union prlist_head *head)
{
	if (!list_empty(&list->list)) {
		__prlist_splice(list, head->prev, head);
		INIT_PRLIST_HEAD(list);
	}
}

/* ---- Protected Doubly Linked List with single pointer list head ---- */
union prhlist_head {
		union prhlist_node *first;
		struct hlist_head head;
} __attribute__((aligned(sizeof(void *))));

union prhlist_node {
	struct {
		union prhlist_node *next;
		union prhlist_node **pprev;
	};
	struct hlist_node node;
} __attribute__((aligned(sizeof(void *))));

#define PRHLIST_HEAD_INIT	{	\
	.head = HLIST_HEAD_INIT,	\
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
void prhlist_set_first(union prhlist_head *head, union prhlist_node *first)
{
	wr_ptr(&head->first, first);
}

static __always_inline
void prhlist_set_next(union prhlist_node *node, union prhlist_node *next)
{
	wr_ptr(&node->next, next);
}

static __always_inline
void prhlist_set_pprev(union prhlist_node *node, union prhlist_node **pprev)
{
	wr_ptr(&node->pprev, pprev);
}

static __always_inline
void prhlist_set_prev(union prhlist_node *node, union prhlist_node *prev)
{
	wr_ptr(node->pprev, prev);
}

static __always_inline void INIT_PRHLIST_HEAD(union prhlist_head *head)
{
	prhlist_set_first(head, NULL);
}

static __always_inline void INIT_PRHLIST_NODE(union prhlist_node *node)
{
	prhlist_set_next(node, NULL);
	prhlist_set_pprev(node, NULL);
}

static __always_inline void __prhlist_del(union prhlist_node *n)
{
	union prhlist_node *next = n->next;
	union prhlist_node **pprev = n->pprev;

	wr_ptr(pprev, next);
	if (next)
		prhlist_set_pprev(next, pprev);
}

static __always_inline void prhlist_del(union prhlist_node *n)
{
	__prhlist_del(n);
	prhlist_set_next(n, LIST_POISON1);
	prhlist_set_pprev(n, LIST_POISON2);
}

static __always_inline void prhlist_del_init(union prhlist_node *n)
{
	if (!hlist_unhashed(&n->node)) {
		__prhlist_del(n);
		INIT_PRHLIST_NODE(n);
	}
}

static __always_inline
void prhlist_add_head(union prhlist_node *n, union prhlist_head *h)
{
	union prhlist_node *first = h->first;

	prhlist_set_next(n, first);
	if (first)
		prhlist_set_pprev(first, &n->next);
	prhlist_set_first(h, n);
	prhlist_set_pprev(n, &h->first);
}

/* next must be != NULL */
static __always_inline
void prhlist_add_before(union prhlist_node *n, union prhlist_node *next)
{
	prhlist_set_pprev(n, next->pprev);
	prhlist_set_next(n, next);
	prhlist_set_pprev(next, &n->next);
	prhlist_set_prev(n, n);
}

static __always_inline
void prhlist_add_behind(union prhlist_node *n, union prhlist_node *prev)
{
	prhlist_set_next(n, prev->next);
	prhlist_set_next(prev, n);
	prhlist_set_pprev(n, &prev->next);
	if (n->next)
		prhlist_set_pprev(n->next, &n->next);
}

/* after that we'll appear to be on some hlist and hlist_del will work */
static __always_inline void prhlist_add_fake(union prhlist_node *n)
{
	prhlist_set_pprev(n, &n->next);
}

/*
 * Move a list from one list head to another. Fixup the pprev
 * reference of the first entry if it exists.
 */
static __always_inline
void prhlist_move_list(union prhlist_head *old, union prhlist_head *new)
{
	prhlist_set_first(new, old->first);
	if (new->first)
		prhlist_set_pprev(new->first, &new->first);
	prhlist_set_first(old, NULL);
}

/* ------------------------ RCU list and hlist ------------------------ */

/*
 * INIT_LIST_HEAD_RCU - Initialize a list_head visible to RCU readers
 * @head: list to be initialized
 *
 * It is exactly equivalent to INIT_LIST_HEAD()
 */
static __always_inline void INIT_PRLIST_HEAD_RCU(union prlist_head *head)
{
	INIT_PRLIST_HEAD(head);
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static __always_inline
void __prlist_add_rcu(union prlist_head *new, union prlist_head *prev,
		      union prlist_head *next)
{
	if (!__list_add_valid(&new->list, &prev->list, &next->list))
		return;
	prlist_set_next(new, next);
	prlist_set_prev(new, prev);
	wr_rcu_assign_pointer(list_next_rcu(&prev->list), new);
	prlist_set_prev(next, new);
}

/**
 * prlist_add_rcu - add a new entry to rcu-protected prlist
 * @new: new entry to be added
 * @head: prlist head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another prlist-mutation primitive, such as prlist_add_rcu()
 * or prlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * list_for_each_entry_rcu().
 */
static __always_inline
void prlist_add_rcu(union prlist_head *new, union prlist_head *head)
{
	__prlist_add_rcu(new, head, head->next);
}

/**
 * prlist_add_tail_rcu - add a new entry to rcu-protected prlist
 * @new: new entry to be added
 * @head: prlist head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another prlist-mutation primitive, such as prlist_add_tail_rcu()
 * or prlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * list_for_each_entry_rcu().
 */
static __always_inline
void prlist_add_tail_rcu(union prlist_head *new, union prlist_head *head)
{
	__prlist_add_rcu(new, head->prev, head);
}

/**
 * prlist_del_rcu - deletes entry from prlist without re-initialization
 * @entry: the element to delete from the prlist.
 *
 * Note: list_empty() on entry does not return true after this,
 * the entry is in an undefined state. It is useful for RCU based
 * lockfree traversal.
 *
 * In particular, it means that we can not poison the forward
 * pointers that may still be used for walking the list.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as prlist_del_rcu()
 * or prlist_add_rcu(), running on this same prlist.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * list_for_each_entry_rcu().
 *
 * Note that the caller is not permitted to immediately free
 * the newly deleted entry.  Instead, either synchronize_rcu()
 * or call_rcu() must be used to defer freeing until an RCU
 * grace period has elapsed.
 */
static __always_inline void prlist_del_rcu(union prlist_head *entry)
{
	__prlist_del_entry(entry);
	prlist_set_prev(entry, LIST_POISON2);
}

/**
 * prhlist_del_init_rcu - deletes entry from hash list with re-initialization
 * @n: the element to delete from the hash list.
 *
 * Note: list_unhashed() on the node return true after this. It is
 * useful for RCU based read lockfree traversal if the writer side
 * must know if the list entry is still hashed or already unhashed.
 *
 * In particular, it means that we can not poison the forward pointers
 * that may still be used for walking the hash list and we can only
 * zero the pprev pointer so list_unhashed() will return true after
 * this.
 *
 * The caller must take whatever precautions are necessary (such as
 * holding appropriate locks) to avoid racing with another
 * list-mutation primitive, such as hlist_add_head_rcu() or
 * hlist_del_rcu(), running on this same list.  However, it is
 * perfectly legal to run concurrently with the _rcu list-traversal
 * primitives, such as hlist_for_each_entry_rcu().
 */
static __always_inline void prhlist_del_init_rcu(union prhlist_node *n)
{
	if (!hlist_unhashed(&n->node)) {
		__prhlist_del(n);
		prhlist_set_pprev(n, NULL);
	}
}

/**
 * prlist_replace_rcu - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * The @old entry will be replaced with the @new entry atomically.
 * Note: @old should not be empty.
 */
static __always_inline
void prlist_replace_rcu(union prlist_head *old, union prlist_head *new)
{
	prlist_set_next(new, old->next);
	prlist_set_prev(new, old->prev);
	wr_rcu_assign_pointer(list_next_rcu(&new->prev->list), new);
	prlist_set_prev(new->next, new);
	prlist_set_prev(old, LIST_POISON2);
}

/**
 * __prlist_splice_init_rcu - join an RCU-protected list into an existing list.
 * @list:	the RCU-protected list to splice
 * @prev:	points to the last element of the existing list
 * @next:	points to the first element of the existing list
 * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
 *
 * The list pointed to by @prev and @next can be RCU-read traversed
 * concurrently with this function.
 *
 * Note that this function blocks.
 *
 * Important note: the caller must take whatever action is necessary to prevent
 * any other updates to the existing list.  In principle, it is possible to
 * modify the list as soon as sync() begins execution. If this sort of thing
 * becomes necessary, an alternative version based on call_rcu() could be
 * created.  But only if -really- needed -- there is no shortage of RCU API
 * members.
 */
static __always_inline
void __prlist_splice_init_rcu(union prlist_head *list,
			      union prlist_head *prev,
			      union prlist_head *next, void (*sync)(void))
{
	union prlist_head *first = list->next;
	union prlist_head *last = list->prev;

	/*
	 * "first" and "last" tracking list, so initialize it.  RCU readers
	 * have access to this list, so we must use INIT_LIST_HEAD_RCU()
	 * instead of INIT_LIST_HEAD().
	 */

	INIT_PRLIST_HEAD_RCU(list);

	/*
	 * At this point, the list body still points to the source list.
	 * Wait for any readers to finish using the list before splicing
	 * the list body into the new list.  Any new readers will see
	 * an empty list.
	 */

	sync();

	/*
	 * Readers are finished with the source list, so perform splice.
	 * The order is important if the new list is global and accessible
	 * to concurrent RCU readers.  Note that RCU readers are not
	 * permitted to traverse the prev pointers without excluding
	 * this function.
	 */

	prlist_set_next(last, next);
	wr_rcu_assign_pointer(list_next_rcu(&prev->list), first);
	prlist_set_prev(first, prev);
	prlist_set_prev(next, last);
}

/**
 * prlist_splice_init_rcu - splice an RCU-protected list into an existing
 *                          list, designed for stacks.
 * @list:	the RCU-protected list to splice
 * @head:	the place in the existing list to splice the first list into
 * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
 */
static __always_inline
void prlist_splice_init_rcu(union prlist_head *list,
			    union prlist_head *head,
			    void (*sync)(void))
{
	if (!list_empty(&list->list))
		__prlist_splice_init_rcu(list, head, head->next, sync);
}

/**
 * prlist_splice_tail_init_rcu - splice an RCU-protected list into an
 *                               existing list, designed for queues.
 * @list:	the RCU-protected list to splice
 * @head:	the place in the existing list to splice the first list into
 * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
 */
static __always_inline
void prlist_splice_tail_init_rcu(union prlist_head *list,
				 union prlist_head *head,
				 void (*sync)(void))
{
	if (!list_empty(&list->list))
		__prlist_splice_init_rcu(list, head->prev, head, sync);
}

/**
 * prhlist_del_rcu - deletes entry from hash list without re-initialization
 * @n: the element to delete from the hash list.
 *
 * Note: list_unhashed() on entry does not return true after this,
 * the entry is in an undefined state. It is useful for RCU based
 * lockfree traversal.
 *
 * In particular, it means that we can not poison the forward
 * pointers that may still be used for walking the hash list.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as hlist_add_head_rcu()
 * or hlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_for_each_entry().
 */
static __always_inline void prhlist_del_rcu(union prhlist_node *n)
{
	__prhlist_del(n);
	prhlist_set_pprev(n, LIST_POISON2);
}

/**
 * prhlist_replace_rcu - replace old entry by new one
 * @old : the element to be replaced
 * @new : the new element to insert
 *
 * The @old entry will be replaced with the @new entry atomically.
 */
static __always_inline
void prhlist_replace_rcu(union prhlist_node *old, union prhlist_node *new)
{
	union prhlist_node *next = old->next;

	prhlist_set_next(new, next);
	prhlist_set_pprev(new, old->pprev);
	wr_rcu_assign_pointer(*(union prhlist_node __rcu **)new->pprev, new);
	if (next)
		prhlist_set_pprev(new->next, &new->next);
	prhlist_set_pprev(old, LIST_POISON2);
}

/**
 * prhlist_add_head_rcu
 * @n: the element to add to the hash list.
 * @h: the list to add to.
 *
 * Description:
 * Adds the specified element to the specified hlist,
 * while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as hlist_add_head_rcu()
 * or hlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.  Regardless of the type of CPU, the
 * list-traversal primitive must be guarded by rcu_read_lock().
 */
static __always_inline
void prhlist_add_head_rcu(union prhlist_node *n, union prhlist_head *h)
{
	union prhlist_node *first = h->first;

	prhlist_set_next(n, first);
	prhlist_set_pprev(n, &h->first);
	wr_rcu_assign_pointer(hlist_first_rcu(&h->head), n);
	if (first)
		prhlist_set_pprev(first, &n->next);
}

/**
 * prhlist_add_tail_rcu
 * @n: the element to add to the hash list.
 * @h: the list to add to.
 *
 * Description:
 * Adds the specified element to the specified hlist,
 * while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as prhlist_add_head_rcu()
 * or prhlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.  Regardless of the type of CPU, the
 * list-traversal primitive must be guarded by rcu_read_lock().
 */
static __always_inline
void prhlist_add_tail_rcu(union prhlist_node *n, union prhlist_head *h)
{
	union prhlist_node *i, *last = NULL;

	/* Note: write side code, so rcu accessors are not needed. */
	for (i = h->first; i; i = i->next)
		last = i;

	if (last) {
		prhlist_set_next(n, last->next);
		prhlist_set_pprev(n, &last->next);
		wr_rcu_assign_pointer(hlist_next_rcu(&last->node), n);
	} else {
		prhlist_add_head_rcu(n, h);
	}
}

/**
 * prhlist_add_before_rcu
 * @n: the new element to add to the hash list.
 * @next: the existing element to add the new element before.
 *
 * Description:
 * Adds the specified element to the specified hlist
 * before the specified node while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as prhlist_add_head_rcu()
 * or prhlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.
 */
static __always_inline
void prhlist_add_before_rcu(union prhlist_node *n, union prhlist_node *next)
{
	prhlist_set_pprev(n, next->pprev);
	prhlist_set_next(n, next);
	wr_rcu_assign_pointer(hlist_pprev_rcu(&n->node), n);
	prhlist_set_pprev(next, &n->next);
}

/**
 * prhlist_add_behind_rcu
 * @n: the new element to add to the hash list.
 * @prev: the existing element to add the new element after.
 *
 * Description:
 * Adds the specified element to the specified hlist
 * after the specified node while permitting racing traversals.
 *
 * The caller must take whatever precautions are necessary
 * (such as holding appropriate locks) to avoid racing
 * with another list-mutation primitive, such as prhlist_add_head_rcu()
 * or prhlist_del_rcu(), running on this same list.
 * However, it is perfectly legal to run concurrently with
 * the _rcu list-traversal primitives, such as
 * hlist_for_each_entry_rcu(), used to prevent memory-consistency
 * problems on Alpha CPUs.
 */
static __always_inline
void prhlist_add_behind_rcu(union prhlist_node *n, union prhlist_node *prev)
{
	prhlist_set_next(n, prev->next);
	prhlist_set_pprev(n, &prev->next);
	wr_rcu_assign_pointer(hlist_next_rcu(&prev->node), n);
	if (n->next)
		prhlist_set_pprev(n->next, &n->next);
}
#endif
