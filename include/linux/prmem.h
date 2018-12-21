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
#include <asm/prmem.h>


/**
 * memtst() - test len bytes starting at p to match the c value
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
#define wr_rcu_assign_pointer(p, v)	rcu_assign_pointer(p, v)

#else

void *wr_memset(void *p, int c, __kernel_size_t len);
void *wr_memcpy(void *p, const void *q, __kernel_size_t size);

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
