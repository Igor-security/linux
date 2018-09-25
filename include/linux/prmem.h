/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmem.h: Header for memory protection library
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Support for:
 * - statically allocated write rare data
 */

#ifndef _LINUX_PRMEM_H
#define _LINUX_PRMEM_H

#include <linux/set_memory.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>

/**
 * memtst() - test n bytes of the source to match the c value
 * @p: beginning of the memory to test
 * @c: byte to compare against
 * @len: amount of bytes to test
 *
 * Returns 0 on success, non-zero otherwise.
 */
static inline int memtst(void *p, int c, __kernel_size_t len)
{
	__kernel_size_t i;

	for (i = 0; i < len; i++) {
		u8 d =  *(i + (u8 *)p) - (u8)c;

		if (unlikely(d))
			return d;
	}
	return 0;
}


#ifndef CONFIG_PRMEM

static inline void *wr_memset(void *p, int c, __kernel_size_t len)
{
	return memset(p, c, len);
}

static inline void *wr_memcpy(void *p, const void *q, __kernel_size_t size)
{
	return memcpy(p, q, size);
}

#define wr_assign(var, val)	((var) = (val))

#define wr_rcu_assign_pointer(p, v)	\
	rcu_assign_pointer(p, v)

#else

enum wr_op_type {
	WR_MEMCPY,
	WR_MEMSET,
	WR_RCU_ASSIGN_PTR,
	WR_OPS_NUMBER,
};

void *__wr_op(unsigned long dst, unsigned long src, __kernel_size_t len,
	      enum wr_op_type op);

/**
 * wr_memset() - sets n bytes of the destination to the c value
 * @p: beginning of the memory to write to
 * @c: byte to replicate
 * @len: amount of bytes to copy
 *
 * Returns true on success, false otherwise.
 */
static inline void *wr_memset(void *p, int c, __kernel_size_t len)
{
	return __wr_op((unsigned long)p, (unsigned long)c, len, WR_MEMSET);
}

/**
 * wr_memcpy() - copyes n bytes from source to destination
 * @dst: beginning of the memory to write to
 * @src: beginning of the memory to read from
 * @n_bytes: amount of bytes to copy
 *
 * Returns pointer to the destination
 */
static inline void *wr_memcpy(void *p, const void *q, __kernel_size_t size)
{
	return __wr_op((unsigned long)p, (unsigned long)q, size, WR_MEMCPY);
}

/**
 * wr_assign() - sets a write-rare variable to a specified value
 * @var: the variable to set
 * @val: the new value
 *
 * Returns: the variable
 *
 * Note: it might be possible to optimize this, to use wr_memset in some
 * cases (maybe with NULL?).
 */

#define wr_assign(var, val) ({			\
	typeof(var) tmp = (typeof(var))val;	\
						\
	wr_memcpy(&var, &tmp, sizeof(var));	\
	var;					\
})

/**
 * wr_rcu_assign_pointer() - initialize a pointer in rcu mode
 * @p: the rcu pointer
 * @v: the new value
 *
 * Returns the value assigned to the rcu pointer.
 *
 * It is provided as macro, to match rcu_assign_pointer()
 */
#define wr_rcu_assign_pointer(p, v) ({				\
	__wr_op((unsigned long)&p, (unsigned long)v, sizeof(p),	\
		WR_RCU_ASSIGN_PTR);				\
	p;							\
})
#endif
#endif
