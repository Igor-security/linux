/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmem.h: Header for memory protection library - generic part
 *
 * (C) Copyright 2018-2019 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PRMEM_H
#define _LINUX_PRMEM_H

#include <linux/set_memory.h>
#include <linux/mutex.h>

#ifndef CONFIG_PRMEM

static inline void *wr_memset(void *p, int c, __kernel_size_t n)
{
	return memset(p, c, n);
}

static inline void *wr_memcpy(void *p, const void *q, __kernel_size_t n)
{
	return memcpy(p, q, n);
}

#define wr_assign(var, val)	((var) = (val))
#define wr_rcu_assign_pointer(p, v)	rcu_assign_pointer(p, v)

#else

#include <linux/mm.h>

void *wr_memset(void *p, int c, __kernel_size_t n);
void *wr_memcpy(void *p, const void *q, __kernel_size_t n);

/**
 * wr_assign() - sets a write-rare variable to a specified value
 * @var: the variable to set
 * @val: the new value
 *
 * Returns: the variable
 */

#define wr_assign(dst, val) ({			\
	typeof(dst) tmp = (typeof(dst))val;	\
						\
	wr_memcpy(&dst, &tmp, sizeof(dst));	\
	dst;					\
})

/**
 * wr_rcu_assign_pointer() - initialize a pointer in rcu mode
 * @p: the rcu pointer - it MUST be aligned to a machine word
 * @v: the new value
 *
 * Returns the value assigned to the rcu pointer.
 *
 * It is provided as macro, to match rcu_assign_pointer()
 * The rcu_assign_pointer() is implemented as equivalent of:
 *
 * smp_mb();
 * WRITE_ONCE();
 */
#define wr_rcu_assign_pointer(p, v) ({	\
	smp_mb();			\
	wr_assign(p, v);		\
	p;				\
})
#endif
#endif
