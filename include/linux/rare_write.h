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

enum rare_write_type {
	RARE_WRITE_VIRT_ADDR,
	RARE_WRITE_VMALLOC_ADDR,
};

/**
 * rare_write_check_boundaries - verifies range correctness
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
 * criteria, depending on the specific use-case and, to minimize
 * run-time checks, also specify the type of memory being modified.
 */
static __always_inline
bool __raw_rare_write(const void *dst, const void *const src,
		      enum rare_write_type type, size_t n_bytes)
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
		if (type == RARE_WRITE_VIRT_ADDR)
			page = virt_to_page(d);
		else if (type == RARE_WRITE_VMALLOC_ADDR)
			page = vmalloc_to_page(d);
		else
			goto err;
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
 * rare_write_array - copies n bytes from source to destination
 * @dst: beginning of the memory to copy to
 * @src: beginning of the memory to copy from
 * @size: amount of bytes to copy
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_array(const void *dst, const void *src, size_t n_bytes)
{

	if (WARN(!rare_write_check_boundaries(dst, n_bytes),
		 "Not a valid rare_write destination."))
		return false;
	return __raw_rare_write(dst, src, RARE_WRITE_VIRT_ADDR, n_bytes);

}

#define __rare_write_simple(dst_ptr, src_ptr)				\
	rare_write_array(dst_ptr, src_ptr, sizeof(*(src_ptr)))

#define __rare_write_safe(dst_ptr, src_ptr,				\
			  unique_dst_ptr, unique_src_ptr)		\
({									\
	typeof(dst_ptr) unique_dst_ptr = (dst_ptr);			\
	typeof(src_ptr) unique_src_ptr = (src_ptr);			\
									\
	rare_write_array(unique_dst_ptr, unique_src_ptr,		\
		     sizeof(*(unique_src_ptr)));			\
})

#define __safe_ops(dst, src)	\
	(__typecheck(dst, src) && __no_side_effects(dst, src))

/**
 * rare_write - copies an object over another of same type and size
 * @dst_ptr: address of the destination object
 * @src_ptr: address of the source object
 */
#define rare_write(dst_ptr, src_ptr)					\
	__builtin_choose_expr(__safe_ops(dst_ptr, src_ptr),		\
			      __rare_write_simple(dst_ptr, src_ptr),	\
			      __rare_write_safe(dst_ptr, src_ptr,	\
						__UNIQUE_ID(__dst_ptr),	\
						__UNIQUE_ID(__src_ptr)))

/* Specialized versions of rare write */

/**
 * rare_write_ptr - alters a pointer in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_ptr(const void *dst, const void *val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_char - alters a char in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_char(const char *dst, const char val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_short - alters a short in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_short(const short *dst, const short val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_ushort - alters an unsigned short in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_ushort(const unsigned short *dst, const unsigned short val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_int - alters an int in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_int(const int *dst, const int val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_uint - alters an unsigned int in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_uint(const unsigned int *dst, const unsigned int val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_long - alters a long in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_long(const long *dst, const long val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_ulong - alters an unsigned long in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_ulong(const unsigned long *dst, const unsigned long val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_longlong - alters a long long in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_longlong(const long long *dst, const long long val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

/**
 * rare_write_ulonglong - alters an unsigned long long in rare-write memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool rare_write_ulonglong(const unsigned long long *dst,
			  const unsigned long long val)
{
	return rare_write_array(dst, &val, sizeof(val));
}

#endif
