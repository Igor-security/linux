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

/* ------------- Circular Protected Doubly Linked List RCU ------------- */

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
static inline void prlist_del_rcu(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->prev = LIST_POISON2;
}

///**
// * hlist_del_init_rcu - deletes entry from hash list with re-initialization
// * @n: the element to delete from the hash list.
// *
// * Note: list_unhashed() on the node return true after this. It is
// * useful for RCU based read lockfree traversal if the writer side
// * must know if the list entry is still hashed or already unhashed.
// *
// * In particular, it means that we can not poison the forward pointers
// * that may still be used for walking the hash list and we can only
// * zero the pprev pointer so list_unhashed() will return true after
// * this.
// *
// * The caller must take whatever precautions are necessary (such as
// * holding appropriate locks) to avoid racing with another
// * list-mutation primitive, such as hlist_add_head_rcu() or
// * hlist_del_rcu(), running on this same list.  However, it is
// * perfectly legal to run concurrently with the _rcu list-traversal
// * primitives, such as hlist_for_each_entry_rcu().
// */
//static inline void hlist_del_init_rcu(struct hlist_node *n)
//{
//	if (!hlist_unhashed(n)) {
//		__hlist_del(n);
//		n->pprev = NULL;
//	}
//}
//
///**
// * list_replace_rcu - replace old entry by new one
// * @old : the element to be replaced
// * @new : the new element to insert
// *
// * The @old entry will be replaced with the @new entry atomically.
// * Note: @old should not be empty.
// */
//static inline void list_replace_rcu(struct list_head *old,
//				struct list_head *new)
//{
//	new->next = old->next;
//	new->prev = old->prev;
//	rcu_assign_pointer(list_next_rcu(new->prev), new);
//	new->next->prev = new;
//	old->prev = LIST_POISON2;
//}
//
///**
// * __list_splice_init_rcu - join an RCU-protected list into an existing list.
// * @list:	the RCU-protected list to splice
// * @prev:	points to the last element of the existing list
// * @next:	points to the first element of the existing list
// * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
// *
// * The list pointed to by @prev and @next can be RCU-read traversed
// * concurrently with this function.
// *
// * Note that this function blocks.
// *
// * Important note: the caller must take whatever action is necessary to prevent
// * any other updates to the existing list.  In principle, it is possible to
// * modify the list as soon as sync() begins execution. If this sort of thing
// * becomes necessary, an alternative version based on call_rcu() could be
// * created.  But only if -really- needed -- there is no shortage of RCU API
// * members.
// */
//static inline void __list_splice_init_rcu(struct list_head *list,
//					  struct list_head *prev,
//					  struct list_head *next,
//					  void (*sync)(void))
//{
//	struct list_head *first = list->next;
//	struct list_head *last = list->prev;
//
//	/*
//	 * "first" and "last" tracking list, so initialize it.  RCU readers
//	 * have access to this list, so we must use INIT_LIST_HEAD_RCU()
//	 * instead of INIT_LIST_HEAD().
//	 */
//
//	INIT_LIST_HEAD_RCU(list);
//
//	/*
//	 * At this point, the list body still points to the source list.
//	 * Wait for any readers to finish using the list before splicing
//	 * the list body into the new list.  Any new readers will see
//	 * an empty list.
//	 */
//
//	sync();
//
//	/*
//	 * Readers are finished with the source list, so perform splice.
//	 * The order is important if the new list is global and accessible
//	 * to concurrent RCU readers.  Note that RCU readers are not
//	 * permitted to traverse the prev pointers without excluding
//	 * this function.
//	 */
//
//	last->next = next;
//	rcu_assign_pointer(list_next_rcu(prev), first);
//	first->prev = prev;
//	next->prev = last;
//}
//
///**
// * list_splice_init_rcu - splice an RCU-protected list into an existing list,
// *                        designed for stacks.
// * @list:	the RCU-protected list to splice
// * @head:	the place in the existing list to splice the first list into
// * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
// */
//static inline void list_splice_init_rcu(struct list_head *list,
//					struct list_head *head,
//					void (*sync)(void))
//{
//	if (!list_empty(list))
//		__list_splice_init_rcu(list, head, head->next, sync);
//}
//
///**
// * list_splice_tail_init_rcu - splice an RCU-protected list into an existing
// *                             list, designed for queues.
// * @list:	the RCU-protected list to splice
// * @head:	the place in the existing list to splice the first list into
// * @sync:	function to sync: synchronize_rcu(), synchronize_sched(), ...
// */
//static inline void list_splice_tail_init_rcu(struct list_head *list,
//					     struct list_head *head,
//					     void (*sync)(void))
//{
//	if (!list_empty(list))
//		__list_splice_init_rcu(list, head->prev, head, sync);
//}
//
///**
// * list_entry_rcu - get the struct for this entry
// * @ptr:        the &struct list_head pointer.
// * @type:       the type of the struct this is embedded in.
// * @member:     the name of the list_head within the struct.
// *
// * This primitive may safely run concurrently with the _rcu list-mutation
// * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
// */
//#define list_entry_rcu(ptr, type, member) \
//	container_of(READ_ONCE(ptr), type, member)
//
///*
// * Where are list_empty_rcu() and list_first_entry_rcu()?
// *
// * Implementing those functions following their counterparts list_empty() and
// * list_first_entry() is not advisable because they lead to subtle race
// * conditions as the following snippet shows:
// *
// * if (!list_empty_rcu(mylist)) {
// *	struct foo *bar = list_first_entry_rcu(mylist, struct foo, list_member);
// *	do_something(bar);
// * }
// *
// * The list may not be empty when list_empty_rcu checks it, but it may be when
// * list_first_entry_rcu rereads the ->next pointer.
// *
// * Rereading the ->next pointer is not a problem for list_empty() and
// * list_first_entry() because they would be protected by a lock that blocks
// * writers.
// *
// * See list_first_or_null_rcu for an alternative.
// */
//
///**
// * list_first_or_null_rcu - get the first element from a list
// * @ptr:        the list head to take the element from.
// * @type:       the type of the struct this is embedded in.
// * @member:     the name of the list_head within the struct.
// *
// * Note that if the list is empty, it returns NULL.
// *
// * This primitive may safely run concurrently with the _rcu list-mutation
// * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
// */
//#define list_first_or_null_rcu(ptr, type, member) \
//({ \
//	struct list_head *__ptr = (ptr); \
//	struct list_head *__next = READ_ONCE(__ptr->next); \
//	likely(__ptr != __next) ? list_entry_rcu(__next, type, member) : NULL; \
//})
//
///**
// * list_next_or_null_rcu - get the first element from a list
// * @head:	the head for the list.
// * @ptr:        the list head to take the next element from.
// * @type:       the type of the struct this is embedded in.
// * @member:     the name of the list_head within the struct.
// *
// * Note that if the ptr is at the end of the list, NULL is returned.
// *
// * This primitive may safely run concurrently with the _rcu list-mutation
// * primitives such as list_add_rcu() as long as it's guarded by rcu_read_lock().
// */
//#define list_next_or_null_rcu(head, ptr, type, member) \
//({ \
//	struct list_head *__head = (head); \
//	struct list_head *__ptr = (ptr); \
//	struct list_head *__next = READ_ONCE(__ptr->next); \
//	likely(__next != __head) ? list_entry_rcu(__next, type, \
//						  member) : NULL; \
//})
//
///**
// * list_for_each_entry_rcu	-	iterate over rcu list of given type
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the list_head within the struct.
// *
// * This list-traversal primitive may safely run concurrently with
// * the _rcu list-mutation primitives such as list_add_rcu()
// * as long as the traversal is guarded by rcu_read_lock().
// */
//#define list_for_each_entry_rcu(pos, head, member) \
//	for (pos = list_entry_rcu((head)->next, typeof(*pos), member); \
//		&pos->member != (head); \
//		pos = list_entry_rcu(pos->member.next, typeof(*pos), member))
//
///**
// * list_entry_lockless - get the struct for this entry
// * @ptr:        the &struct list_head pointer.
// * @type:       the type of the struct this is embedded in.
// * @member:     the name of the list_head within the struct.
// *
// * This primitive may safely run concurrently with the _rcu list-mutation
// * primitives such as list_add_rcu(), but requires some implicit RCU
// * read-side guarding.  One example is running within a special
// * exception-time environment where preemption is disabled and where
// * lockdep cannot be invoked (in which case updaters must use RCU-sched,
// * as in synchronize_sched(), call_rcu_sched(), and friends).  Another
// * example is when items are added to the list, but never deleted.
// */
//#define list_entry_lockless(ptr, type, member) \
//	container_of((typeof(ptr))READ_ONCE(ptr), type, member)
//
///**
// * list_for_each_entry_lockless - iterate over rcu list of given type
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the list_struct within the struct.
// *
// * This primitive may safely run concurrently with the _rcu list-mutation
// * primitives such as list_add_rcu(), but requires some implicit RCU
// * read-side guarding.  One example is running within a special
// * exception-time environment where preemption is disabled and where
// * lockdep cannot be invoked (in which case updaters must use RCU-sched,
// * as in synchronize_sched(), call_rcu_sched(), and friends).  Another
// * example is when items are added to the list, but never deleted.
// */
//#define list_for_each_entry_lockless(pos, head, member) \
//	for (pos = list_entry_lockless((head)->next, typeof(*pos), member); \
//	     &pos->member != (head); \
//	     pos = list_entry_lockless(pos->member.next, typeof(*pos), member))
//
///**
// * list_for_each_entry_continue_rcu - continue iteration over list of given type
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the list_head within the struct.
// *
// * Continue to iterate over list of given type, continuing after
// * the current position which must have been in the list when the RCU read
// * lock was taken.
// * This would typically require either that you obtained the node from a
// * previous walk of the list in the same RCU read-side critical section, or
// * that you held some sort of non-RCU reference (such as a reference count)
// * to keep the node alive *and* in the list.
// *
// * This iterator is similar to list_for_each_entry_from_rcu() except
// * this starts after the given position and that one starts at the given
// * position.
// */
//#define list_for_each_entry_continue_rcu(pos, head, member) 		\
//	for (pos = list_entry_rcu(pos->member.next, typeof(*pos), member); \
//	     &pos->member != (head);	\
//	     pos = list_entry_rcu(pos->member.next, typeof(*pos), member))
//
///**
// * list_for_each_entry_from_rcu - iterate over a list from current point
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the list_node within the struct.
// *
// * Iterate over the tail of a list starting from a given position,
// * which must have been in the list when the RCU read lock was taken.
// * This would typically require either that you obtained the node from a
// * previous walk of the list in the same RCU read-side critical section, or
// * that you held some sort of non-RCU reference (such as a reference count)
// * to keep the node alive *and* in the list.
// *
// * This iterator is similar to list_for_each_entry_continue_rcu() except
// * this starts from the given position and that one starts from the position
// * after the given position.
// */
//#define list_for_each_entry_from_rcu(pos, head, member)			\
//	for (; &(pos)->member != (head);					\
//		pos = list_entry_rcu(pos->member.next, typeof(*(pos)), member))
//
///**
// * hlist_del_rcu - deletes entry from hash list without re-initialization
// * @n: the element to delete from the hash list.
// *
// * Note: list_unhashed() on entry does not return true after this,
// * the entry is in an undefined state. It is useful for RCU based
// * lockfree traversal.
// *
// * In particular, it means that we can not poison the forward
// * pointers that may still be used for walking the hash list.
// *
// * The caller must take whatever precautions are necessary
// * (such as holding appropriate locks) to avoid racing
// * with another list-mutation primitive, such as hlist_add_head_rcu()
// * or hlist_del_rcu(), running on this same list.
// * However, it is perfectly legal to run concurrently with
// * the _rcu list-traversal primitives, such as
// * hlist_for_each_entry().
// */
//static inline void hlist_del_rcu(struct hlist_node *n)
//{
//	__hlist_del(n);
//	n->pprev = LIST_POISON2;
//}
//
///**
// * hlist_replace_rcu - replace old entry by new one
// * @old : the element to be replaced
// * @new : the new element to insert
// *
// * The @old entry will be replaced with the @new entry atomically.
// */
//static inline void hlist_replace_rcu(struct hlist_node *old,
//					struct hlist_node *new)
//{
//	struct hlist_node *next = old->next;
//
//	new->next = next;
//	new->pprev = old->pprev;
//	rcu_assign_pointer(*(struct hlist_node __rcu **)new->pprev, new);
//	if (next)
//		new->next->pprev = &new->next;
//	old->pprev = LIST_POISON2;
//}
//
///*
// * return the first or the next element in an RCU protected hlist
// */
//#define hlist_first_rcu(head)	(*((struct hlist_node __rcu **)(&(head)->first)))
//#define hlist_next_rcu(node)	(*((struct hlist_node __rcu **)(&(node)->next)))
//#define hlist_pprev_rcu(node)	(*((struct hlist_node __rcu **)((node)->pprev)))
//
///**
// * hlist_add_head_rcu
// * @n: the element to add to the hash list.
// * @h: the list to add to.
// *
// * Description:
// * Adds the specified element to the specified hlist,
// * while permitting racing traversals.
// *
// * The caller must take whatever precautions are necessary
// * (such as holding appropriate locks) to avoid racing
// * with another list-mutation primitive, such as hlist_add_head_rcu()
// * or hlist_del_rcu(), running on this same list.
// * However, it is perfectly legal to run concurrently with
// * the _rcu list-traversal primitives, such as
// * hlist_for_each_entry_rcu(), used to prevent memory-consistency
// * problems on Alpha CPUs.  Regardless of the type of CPU, the
// * list-traversal primitive must be guarded by rcu_read_lock().
// */
//static inline void hlist_add_head_rcu(struct hlist_node *n,
//					struct hlist_head *h)
//{
//	struct hlist_node *first = h->first;
//
//	n->next = first;
//	n->pprev = &h->first;
//	rcu_assign_pointer(hlist_first_rcu(h), n);
//	if (first)
//		first->pprev = &n->next;
//}
//
///**
// * hlist_add_tail_rcu
// * @n: the element to add to the hash list.
// * @h: the list to add to.
// *
// * Description:
// * Adds the specified element to the specified hlist,
// * while permitting racing traversals.
// *
// * The caller must take whatever precautions are necessary
// * (such as holding appropriate locks) to avoid racing
// * with another list-mutation primitive, such as hlist_add_head_rcu()
// * or hlist_del_rcu(), running on this same list.
// * However, it is perfectly legal to run concurrently with
// * the _rcu list-traversal primitives, such as
// * hlist_for_each_entry_rcu(), used to prevent memory-consistency
// * problems on Alpha CPUs.  Regardless of the type of CPU, the
// * list-traversal primitive must be guarded by rcu_read_lock().
// */
//static inline void hlist_add_tail_rcu(struct hlist_node *n,
//				      struct hlist_head *h)
//{
//	struct hlist_node *i, *last = NULL;
//
//	/* Note: write side code, so rcu accessors are not needed. */
//	for (i = h->first; i; i = i->next)
//		last = i;
//
//	if (last) {
//		n->next = last->next;
//		n->pprev = &last->next;
//		rcu_assign_pointer(hlist_next_rcu(last), n);
//	} else {
//		hlist_add_head_rcu(n, h);
//	}
//}
//
///**
// * hlist_add_before_rcu
// * @n: the new element to add to the hash list.
// * @next: the existing element to add the new element before.
// *
// * Description:
// * Adds the specified element to the specified hlist
// * before the specified node while permitting racing traversals.
// *
// * The caller must take whatever precautions are necessary
// * (such as holding appropriate locks) to avoid racing
// * with another list-mutation primitive, such as hlist_add_head_rcu()
// * or hlist_del_rcu(), running on this same list.
// * However, it is perfectly legal to run concurrently with
// * the _rcu list-traversal primitives, such as
// * hlist_for_each_entry_rcu(), used to prevent memory-consistency
// * problems on Alpha CPUs.
// */
//static inline void hlist_add_before_rcu(struct hlist_node *n,
//					struct hlist_node *next)
//{
//	n->pprev = next->pprev;
//	n->next = next;
//	rcu_assign_pointer(hlist_pprev_rcu(n), n);
//	next->pprev = &n->next;
//}
//
///**
// * hlist_add_behind_rcu
// * @n: the new element to add to the hash list.
// * @prev: the existing element to add the new element after.
// *
// * Description:
// * Adds the specified element to the specified hlist
// * after the specified node while permitting racing traversals.
// *
// * The caller must take whatever precautions are necessary
// * (such as holding appropriate locks) to avoid racing
// * with another list-mutation primitive, such as hlist_add_head_rcu()
// * or hlist_del_rcu(), running on this same list.
// * However, it is perfectly legal to run concurrently with
// * the _rcu list-traversal primitives, such as
// * hlist_for_each_entry_rcu(), used to prevent memory-consistency
// * problems on Alpha CPUs.
// */
//static inline void hlist_add_behind_rcu(struct hlist_node *n,
//					struct hlist_node *prev)
//{
//	n->next = prev->next;
//	n->pprev = &prev->next;
//	rcu_assign_pointer(hlist_next_rcu(prev), n);
//	if (n->next)
//		n->next->pprev = &n->next;
//}
//
//#define __hlist_for_each_rcu(pos, head)				\
//	for (pos = rcu_dereference(hlist_first_rcu(head));	\
//	     pos;						\
//	     pos = rcu_dereference(hlist_next_rcu(pos)))
//
///**
// * hlist_for_each_entry_rcu - iterate over rcu list of given type
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the hlist_node within the struct.
// *
// * This list-traversal primitive may safely run concurrently with
// * the _rcu list-mutation primitives such as hlist_add_head_rcu()
// * as long as the traversal is guarded by rcu_read_lock().
// */
//#define hlist_for_each_entry_rcu(pos, head, member)			\
//	for (pos = hlist_entry_safe (rcu_dereference_raw(hlist_first_rcu(head)),\
//			typeof(*(pos)), member);			\
//		pos;							\
//		pos = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(\
//			&(pos)->member)), typeof(*(pos)), member))
//
///**
// * hlist_for_each_entry_rcu_notrace - iterate over rcu list of given type (for tracing)
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the hlist_node within the struct.
// *
// * This list-traversal primitive may safely run concurrently with
// * the _rcu list-mutation primitives such as hlist_add_head_rcu()
// * as long as the traversal is guarded by rcu_read_lock().
// *
// * This is the same as hlist_for_each_entry_rcu() except that it does
// * not do any RCU debugging or tracing.
// */
//#define hlist_for_each_entry_rcu_notrace(pos, head, member)			\
//	for (pos = hlist_entry_safe (rcu_dereference_raw_notrace(hlist_first_rcu(head)),\
//			typeof(*(pos)), member);			\
//		pos;							\
//		pos = hlist_entry_safe(rcu_dereference_raw_notrace(hlist_next_rcu(\
//			&(pos)->member)), typeof(*(pos)), member))
//
///**
// * hlist_for_each_entry_rcu_bh - iterate over rcu list of given type
// * @pos:	the type * to use as a loop cursor.
// * @head:	the head for your list.
// * @member:	the name of the hlist_node within the struct.
// *
// * This list-traversal primitive may safely run concurrently with
// * the _rcu list-mutation primitives such as hlist_add_head_rcu()
// * as long as the traversal is guarded by rcu_read_lock().
// */
//#define hlist_for_each_entry_rcu_bh(pos, head, member)			\
//	for (pos = hlist_entry_safe(rcu_dereference_bh(hlist_first_rcu(head)),\
//			typeof(*(pos)), member);			\
//		pos;							\
//		pos = hlist_entry_safe(rcu_dereference_bh(hlist_next_rcu(\
//			&(pos)->member)), typeof(*(pos)), member))
//
///**
// * hlist_for_each_entry_continue_rcu - iterate over a hlist continuing after current point
// * @pos:	the type * to use as a loop cursor.
// * @member:	the name of the hlist_node within the struct.
// */
//#define hlist_for_each_entry_continue_rcu(pos, member)			\
//	for (pos = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu( \
//			&(pos)->member)), typeof(*(pos)), member);	\
//	     pos;							\
//	     pos = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(	\
//			&(pos)->member)), typeof(*(pos)), member))
//
///**
// * hlist_for_each_entry_continue_rcu_bh - iterate over a hlist continuing after current point
// * @pos:	the type * to use as a loop cursor.
// * @member:	the name of the hlist_node within the struct.
// */
//#define hlist_for_each_entry_continue_rcu_bh(pos, member)		\
//	for (pos = hlist_entry_safe(rcu_dereference_bh(hlist_next_rcu(  \
//			&(pos)->member)), typeof(*(pos)), member);	\
//	     pos;							\
//	     pos = hlist_entry_safe(rcu_dereference_bh(hlist_next_rcu(	\
//			&(pos)->member)), typeof(*(pos)), member))
//
///**
// * hlist_for_each_entry_from_rcu - iterate over a hlist continuing from current point
// * @pos:	the type * to use as a loop cursor.
// * @member:	the name of the hlist_node within the struct.
// */
//#define hlist_for_each_entry_from_rcu(pos, member)			\
//	for (; pos;							\
//	     pos = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(	\
//			&(pos)->member)), typeof(*(pos)), member))
//
//
#endif
