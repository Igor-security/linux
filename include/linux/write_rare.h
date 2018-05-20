/* SPDX-License-Identifier: GPL-2.0 */
/*
 * write_rare.h: Header for rare writes to statically allocated variables
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Support for modifying "variables" residing in protected memory.
 * They are write-protected against direct writes and can be altered only
 * through special means.
 */

#ifndef _LINUX_WRITE_RARE_H
#define _LINUX_WRITE_RARE_H

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>

extern long __start_wr_after_init;
extern long __end_wr_after_init;

/**
 * wr_check_boundaries - verifies range correctness
 * @dst: beginning of the memory to alter
 * @size: amount of bytes to alter
 *
 * Returns true on range ok, false on error.
 *
 * This test is meant to verify that the range beign altered is within the
 * memory segment reserved for re-writable memory and doesn't overlap any
 * other area.
 */
static __always_inline
bool wr_check_boundaries(const void *dst, size_t size)
{
	size_t start = (size_t)&__start_wr_after_init;
	size_t end = (size_t)&__end_wr_after_init;
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
bool __raw_wr(const void *dst, const void *const src, size_t n_bytes)
{
	size_t size;
	unsigned long flags;
	const void *d = dst;
	const void *s = src;

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
		if (WARN(!base, "failed to remap rare-write page"))
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

/**
 * wr_array - copies n bytes from source to destination
 * @dst: beginning of the memory to copy to
 * @src: beginning of the memory to copy from
 * @size: amount of bytes to copy
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_array(const void *dst, const void *src, size_t n_bytes)
{
	if (WARN(!wr_check_boundaries(dst, n_bytes),
		 "Not a valid write rare destination."))
		return false;
	return __raw_wr(dst, src, n_bytes);

}

#define __wr_simple(dst_ptr, src_ptr)					\
	wr_array(dst_ptr, src_ptr, sizeof(*(src_ptr)))

#define __wr_safe(dst_ptr, src_ptr,					\
		  unique_dst_ptr, unique_src_ptr)			\
({									\
	typeof(dst_ptr) unique_dst_ptr = (dst_ptr);			\
	typeof(src_ptr) unique_src_ptr = (src_ptr);			\
									\
	wr_array(unique_dst_ptr, unique_src_ptr,			\
		     sizeof(*(unique_src_ptr)));			\
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

/* Specialized versions of rare write */

/**
 * wr_ptr - alters a pointer in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ptr(const void *dst, const void *val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_char - alters a char in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_char(const char *dst, const char val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_short - alters a short in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_short(const short *dst, const short val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_ushort - alters an unsigned short in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ushort(const unsigned short *dst, const unsigned short val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_int - alters an int in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_int(const int *dst, const int val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_uint - alters an unsigned int in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_uint(const unsigned int *dst, const unsigned int val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_long - alters a long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_long(const long *dst, const long val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_ulong - alters an unsigned long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ulong(const unsigned long *dst, const unsigned long val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_longlong - alters a long long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_longlong(const long long *dst, const long long val)
{
	return wr_array(dst, &val, sizeof(val));
}

/**
 * wr_ulonglong - alters an unsigned long long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ulonglong(const unsigned long long *dst,
			  const unsigned long long val)
{
	return wr_array(dst, &val, sizeof(val));
}

#endif
