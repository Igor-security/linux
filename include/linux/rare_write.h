/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rare_write.h: Header for rare writes to statically allocated variables
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Support for modifying "variables" residing in protected memory.
 * They are write-protected against direct writes and can be altered only
 * through special means.
 */

#ifndef _LINUX_RARE_WRITE_H
#define _LINUX_RARE_WRITE_H

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>

extern long __start_rare_write_after_init;
extern long __end_rare_write_after_init;

static __always_inline
bool rare_write_check_boundaries(const void *dst, size_t size)
{
	size_t start = (size_t)&__start_rare_write_after_init;
	size_t end = (size_t)&__end_rare_write_after_init;
	size_t low = (size_t)dst;
	size_t high = (size_t)dst + size;

	return likely(start <= low && low < high && high <= end);
}

/*
 * This is the core of the rare write functionality.
 * It doesn't perform any check on the validity of the target.
 * The wrapper using it is supposed to apply sensible verification
 * criteria.
 */
static __always_inline
bool __raw_rare_write(const void *dst, const void *src, size_t n_bytes)
{
	size_t size;
	unsigned long flags;

	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		local_irq_save(flags);
		page = virt_to_page(dst);
		offset = (unsigned long)dst & ~PAGE_MASK;
		offset_complement = ((size_t)PAGE_SIZE) - offset;
		size = min(((int)n_bytes), ((int)offset_complement));
		base = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		if (WARN(!base, "failed to remap rare-write page"))
			goto err;
		__write_once_size(base + offset, src, size);
		vunmap(base);
		dst += size;
		src += size;
		n_bytes -= size;
		local_irq_restore(flags);
	}
	return true;
err:
	local_irq_restore(flags);
	return false;
}

static __always_inline
bool __rare_write(const void *dst, const void *src, size_t n_bytes)
{

	if (WARN(!rare_write_check_boundaries(dst, n_bytes),
		 "Not a valid rare_write destination."))
		return false;
	return __raw_rare_write(dst, src, n_bytes);

}

static __always_inline
bool rare_write_char(const char *dst, const char val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_short(const short *dst, const short val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_ushort(const unsigned short *dst, const unsigned short val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_int(const int *dst, const int val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_uint(const unsigned int *dst, const unsigned int val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_long(const long *dst, const long val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_ulong(const unsigned long *dst, const unsigned long val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_longlong(const long long *dst, const long long val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_ulonglong(const unsigned long long *dst,
			  const unsigned long long val)
{
	return __rare_write(dst, &val, sizeof(val));
}

static __always_inline
bool rare_write_ptr(const void *dst, const void *val)
{
	return __rare_write(dst, &val, sizeof(val));
}

#define __rare_write_simple(dst_ptr, src_ptr)				\
	__rare_write(dst_ptr, src_ptr, sizeof(*(src_ptr)))

#define __rare_write_safe(dst_ptr, src_ptr,				\
			  unique_dst_ptr, unique_src_ptr)		\
({									\
	typeof(dst_ptr) unique_dst_ptr = (dst_ptr);			\
	typeof(src_ptr) unique_src_ptr = (src_ptr);			\
									\
	__rare_write(unique_dst_ptr, unique_src_ptr,			\
		     sizeof(*(unique_src_ptr)));			\
})

#define __safe_ops(dst, src)	\
	(__typecheck(dst, src) && __no_side_effects(dst, src))

#define rare_write(dst_ptr, src_ptr)					\
	__builtin_choose_expr(__safe_ops(dst_ptr, src_ptr),		\
			      __rare_write_simple(dst_ptr, src_ptr),	\
			      __rare_write_safe(dst_ptr, src_ptr,	\
						__UNIQUE_ID(__dst_ptr),	\
						__UNIQUE_ID(__src_ptr)))

#define rare_write_array(dst_ptr, src_ptr, size)			\
	__rare_write(dst_ptr, src_ptr, size)
#endif
