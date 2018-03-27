/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmem.h: Header for memory protection library
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Support for:
 * - statically allocated write rare data
 * - dynamically allocated read only data
 * - dynamically allocated write rare data
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

static __always_inline bool __is_wr_pool(const void *ptr, size_t size)
{
	struct vmap_area *area;

	if (!is_vmalloc_addr(ptr))
		return false;
	area = find_vmap_area((unsigned long)ptr);
	return area && area->vm && (area->vm->size >= size) &&
		((area->vm->flags & (VM_PMALLOC | VM_PMALLOC_WR)) ==
		 (VM_PMALLOC | VM_PMALLOC_WR));
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
	bool is_virt = __is_wr_after_init(dst, n_bytes);

	if (!(is_virt || likely(__is_wr_pool(dst, n_bytes))))
		return !WARN_ON("Write rare on invalid memory range.");
	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		local_irq_save(flags);
		page = is_virt ? virt_to_page(d) : vmalloc_to_page(d);
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
	bool is_virt = __is_wr_after_init(dst, n_bytes);

	if (!(is_virt || likely(__is_wr_pool(dst, n_bytes))))
		return !WARN_ON("Write rare on invalid memory range.");
	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		local_irq_save(flags);
		page = is_virt ? virt_to_page(d) : vmalloc_to_page(d);
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

/* ============================ Allocator ============================ */

#define PMALLOC_REFILL_DEFAULT (0)
#define PMALLOC_DEFAULT_REFILL_SIZE PAGE_SIZE
#define PMALLOC_ALIGN_ORDER_DEFAULT ilog2(ARCH_KMALLOC_MINALIGN)

#define PMALLOC_RO		0x00
#define PMALLOC_WR		0x01
#define PMALLOC_AUTO		0x02
#define PMALLOC_START		0x04

#define PMALLOC_MASK		(PMALLOC_WR | PMALLOC_AUTO | PMALLOC_START)
#define PMALLOC_AUTO_RO		(PMALLOC_RO | PMALLOC_AUTO)
#define PMALLOC_AUTO_WR		(PMALLOC_WR | PMALLOC_AUTO)
#define PMALLOC_START_WR	(PMALLOC_WR | PMALLOC_START)

/*
 * Some of the definitions below are duplicated, only for the purpose of
 * having names which are more expressive in different contexts.
 */
#define VM_PMALLOC_MASK \
		(VM_PMALLOC | VM_PMALLOC_WR | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_RO_MASK		(VM_PMALLOC | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_WR_MASK		VM_PMALLOC_MASK
#define VM_PMALLOC_PROTECTED_MASK	VM_PMALLOC_RO_MASK


struct pmalloc_pool {
	struct mutex mutex;
	struct list_head pool_node;
	struct vmap_area *area;
	size_t align;
	size_t refill;
	size_t offset;
	uint8_t mode;
};

/*
 * The write rare functionality is fully implemented as __always_inline,
 * to prevent having an internal function call that is capable of modifying
 * write protected memory.
 * Fully inlining the function allows the compiler to optimize away its
 * interface, making it harder for an attacker to hijack it.
 * This still leaves the door open to attacks that might try to reuse part
 * of the code, by jumping in the middle of the function, however it can
 * be mitigated by having a compiler plugin that enforces Control Flow
 * Integrity (CFI).
 * Any addition/modification to the write rare path must follow the same
 * approach.
 */

void pmalloc_init_custom_pool(struct pmalloc_pool *pool, size_t refill,
			      short align_order, uint8_t mode);

struct pmalloc_pool *pmalloc_create_custom_pool(size_t refill,
						short align_order,
						uint8_t mode);

/**
 * pmalloc_create_pool() - create a protectable memory pool
 * @mode: can the data be altered after protection
 *
 * Shorthand for pmalloc_create_custom_pool() with default argument:
 * * refill is set to PMALLOC_REFILL_DEFAULT
 * * align_order is set to PMALLOC_ALIGN_ORDER_DEFAULT
 *
 * Returns:
 * * pointer to the new pool	- success
 * * NULL			- error
 */
static inline struct pmalloc_pool *pmalloc_create_pool(uint8_t mode)
{
	return pmalloc_create_custom_pool(PMALLOC_REFILL_DEFAULT,
					  PMALLOC_ALIGN_ORDER_DEFAULT,
					  mode);
}

void *pmalloc(struct pmalloc_pool *pool, size_t size);

/**
 * pzalloc() - zero-initialized version of pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @size: amount of memory (in bytes) requested
 *
 * Executes pmalloc(), initializing the memory requested to 0, before
 * returning its address.
 *
 * Return:
 * * pointer to the memory requested	- success
 * * NULL				- error
 */
static inline void *pzalloc(struct pmalloc_pool *pool, size_t size)
{
	void *ptr = pmalloc(pool, size);

	if (unlikely(!ptr))
		return ptr;
	if ((pool->mode & PMALLOC_START_WR) == PMALLOC_START_WR)
		wr_memset(ptr, 0, size);
	else
		memset(ptr, 0, size);
	return ptr;
}

/**
 * pmalloc_array() - array version of pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested for each element
 *
 * Executes pmalloc(), on an array.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */

static inline
void *pmalloc_array(struct pmalloc_pool *pool, size_t n, size_t size)
{
	size_t total_size = n * size;

	if (unlikely(!(n && (total_size / n == size))))
		return NULL;
	return pmalloc(pool, n * size);
}

/**
 * pcalloc() - array version of pzalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @n: number of elements in the array
 * @size: amount of memory (in bytes) requested for each element
 *
 * Executes pzalloc(), on an array.
 *
 * Return:
 * * the pmalloc result	- success
 * * NULL		- error
 */
static inline
void *pcalloc(struct pmalloc_pool *pool, size_t n, size_t size)
{
	size_t total_size = n * size;

	if (unlikely(!(n && (total_size / n == size))))
		return NULL;
	return pzalloc(pool, n * size);
}

/**
 * pstrdup() - duplicate a string, using pmalloc()
 * @pool: handle to the pool to be used for memory allocation
 * @s: string to duplicate
 *
 * Generates a copy of the given string, allocating sufficient memory
 * from the given pmalloc pool.
 *
 * Return:
 * * pointer to the replica	- success
 * * NULL			- error
 */
static inline char *pstrdup(struct pmalloc_pool *pool, const char *s)
{
	size_t len;
	char *buf;

	len = strlen(s) + 1;
	buf = pmalloc(pool, len);
	if (unlikely(!buf))
		return buf;
	if ((pool->mode & PMALLOC_START_WR) == PMALLOC_START_WR)
		wr_memcpy(buf, s, len);
	else
		strncpy(buf, s, len);
	return buf;
}


void pmalloc_protect_pool(struct pmalloc_pool *pool);

void pmalloc_make_pool_ro(struct pmalloc_pool *pool);

void pmalloc_destroy_pool(struct pmalloc_pool *pool);

#endif
