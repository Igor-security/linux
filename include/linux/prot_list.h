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

struct prot_list_pool {
	struct pmalloc_pool pool;
};

struct prot_head {
	struct prot_head *next, *prev;
};

struct prot_list_pool *prot_list_create_custom_pool(size_t refill,
						    unsigned short align_order);

static inline
struct prot_list_pool *prot_list_create_pool(void)
{
	return prot_list_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					    PMALLOC_ALIGN_DEFAULT);
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

#define prot_list_append(pool, head, src, node) \
	__prot_list_add(pool, head, src, sizeof(*src), \
			((uintptr_t)&(src)->node) - (uintptr_t)(src))

#define prot_list_prepend(pool, head, src, node) \
	__prot_list_add(pool, (head)->prev, src, sizeof(*(src)), \
			((uintptr_t)&(src)->node) - (uintptr_t)(src))

static inline bool __prot_list_add(struct prot_list_pool *pool,
				   struct prot_head *head,
				   void *src, size_t src_size,
				   uintptr_t offset)
{
	void *dst;
	bool retval;
	struct prot_head *src_list;
	void *p;

	dst = pmalloc(&pool->pool, src_size);
	if (WARN(!head, "Could not allocate protected list head."))
		return false;
	mutex_lock(&pool->pool.mutex);
	src_list = src + offset;
	src_list->prev = head;
	src_list->next = head->next;
	retval = pmalloc_rare_write(&pool->pool, dst, src, src_size);
	if (WARN(!retval, "Failed to init list element."))
		goto out;
	p = (void *)(offset + (uintptr_t)dst);
	retval = pmalloc_rare_write(&pool->pool, &head->next->prev, &p,
				      sizeof(p));
	if (WARN(!retval, "Failed to hook to next element."))
		goto out;
	retval = pmalloc_rare_write(&pool->pool, &head->next, &p, sizeof(p));
	if (WARN(!retval, "Failed to hook to previous element."))
		goto out;
out:
	mutex_unlock(&pool->pool.mutex);
	return retval;
}
#endif
