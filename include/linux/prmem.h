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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/set_memory.h>

/* ============================ Write Rare ============================ */

/*
 * The following two variables are statically allocated by the linker
 * script at the the boundaries of the memory region (rounded up to
 * multiples of PAGE_SIZE) reserved for __wr_after_init.
 */
extern long __start_wr_after_init;
extern long __end_wr_after_init;

static __always_inline bool __is_wr_after_init(const void *ptr, size_t size)
{
	size_t start = (size_t)&__start_wr_after_init;
	size_t end = (size_t)&__end_wr_after_init;
	size_t low = (size_t)ptr;
	size_t high = (size_t)ptr + size;

	return likely(start <= low && low < high && high <= end);
}

/**
 * wr_memset() - sets n bytes of the destination to the c value
 * @dst: beginning of the memory to write to
 * @c: byte to replicate
 * @size: amount of bytes to copy
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_memset(const void *dst, const int c, size_t n_bytes)
{
	size_t size;
	unsigned long flags;
	const void *d = dst;

	if (!__is_wr_after_init(dst, n_bytes))
		return !WARN_ON("Write rare on invalid memory range.");
	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		local_irq_save(flags);
		page = virt_to_page(d);
		offset = (unsigned long)d & ~PAGE_MASK;
		offset_complement = ((size_t)PAGE_SIZE) - offset;
		size = min(((int)n_bytes), ((int)offset_complement));
		base = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		if (WARN(!base, "failed to remap write rare page"))
			goto err;
		memset(base + offset, c, size);
		vunmap(base);
		d += size;
		n_bytes -= size;
		local_irq_restore(flags);
	}
	return true;
err:
	local_irq_restore(flags);
	return false;
}

/**
 * wr_memcpy() - copyes n bytes from source to destination
 * @dst: beginning of the memory to write to
 * @src: beginning of the memory to read from
 * @n_bytes: amount of bytes to copy
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_memcpy(const void *dst, const void *src, size_t n_bytes)
{
	size_t size;
	unsigned long flags;
	const void *d = dst;
	const void *s = src;

	if (!__is_wr_after_init(dst, n_bytes))
		return !WARN_ON("Write rare on invalid memory range.");
	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		local_irq_save(flags);
		page = virt_to_page(d);
		offset = (unsigned long)d & ~PAGE_MASK;
		offset_complement = ((size_t)PAGE_SIZE) - offset;
		size = min(((int)n_bytes), ((int)offset_complement));
		base = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		if (WARN(!base, "failed to remap write rare page"))
			goto err;
		__write_once_size(base + offset, (void *)s, size);
		vunmap(base);
		d += size;
		s += size;
		n_bytes -= size;
		local_irq_restore(flags);
	}
	return true;
err:
	local_irq_restore(flags);
	return false;
}

#define __wr_simple(dst_ptr, src_ptr)					\
	wr_memcpy(dst_ptr, src_ptr, sizeof(*(src_ptr)))

#define __wr_safe(dst_ptr, src_ptr,					\
		  unique_dst_ptr, unique_src_ptr)			\
({									\
	typeof(dst_ptr) unique_dst_ptr = (dst_ptr);			\
	typeof(src_ptr) unique_src_ptr = (src_ptr);			\
									\
	wr_memcpy(unique_dst_ptr, unique_src_ptr,			\
		  sizeof(*(unique_src_ptr)));				\
})

#define __safe_ops(dst, src)	\
	(__typecheck(dst, src) && __no_side_effects(dst, src))

/**
 * wr - copies an object over another of same type and size
 * @dst_ptr: address of the destination object
 * @src_ptr: address of the source object
 */
#define wr(dst_ptr, src_ptr)						\
	__builtin_choose_expr(__safe_ops(dst_ptr, src_ptr),		\
			      __wr_simple(dst_ptr, src_ptr),		\
			      __wr_safe(dst_ptr, src_ptr,		\
						__UNIQUE_ID(__dst_ptr),	\
						__UNIQUE_ID(__src_ptr)))

/**
 * wr_ptr() - alters a pointer in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ptr(const void *dst, const void *val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

#endif
