/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmalloc.h: Header for Protectable Memory Allocator
 *
 * (C) Copyright 2017-18 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#ifndef _LINUX_PMALLOC_H
#define _LINUX_PMALLOC_H

#ifndef CONFIG_PROTECTABLE_MEMORY

/**
 * check_pmalloc_object - hardened usercopy stub if pmalloc is unavailable
 * @ptr: the beginning of the memory to check
 * @n: the size of the memory to check
 * @to_user: copy to userspace or from userspace
 *
 * If pmalloc is disabled, there is nothing to check.
 */
static inline
void check_pmalloc_object(const void *ptr, unsigned long n, bool to_user)
{
}

#else


#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/rare_write.h>
/*
 * Library for dynamic allocation of pools of protectable memory.
 * A pool is a single linked list of vmap_area structures.
 * Whenever a pool is protected, all the areas it contains at that point
 * are write protected.
 * More areas can be added and protected, in the same way.
 * Memory in a pool cannot be individually unprotected, but the pool can
 * be destroyed.
 * Upon destruction of a certain pool, all the related memory is released,
 * including its metadata.
 *
 * Depending on the type of protection that was chosen, the memory can be
 * either completely read-only or it can support rare-writes.
 *
 * The rare-write mechanism is intended to provide no read overhead and
 * still some form of protection, while a selected area is modified.
 * This will incur into a penalty that is partially depending on the
 * specific architecture, but in general is the price to pay for limiting
 * the attack surface, while the change takes place.
 *
 * For additional safety, it is not possible to have in the same pool both
 * rare-write and unmodifiable memory.
 */

#include <linux/set_memory.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/llist.h>

#define PMALLOC_REFILL_DEFAULT (0)
#define PMALLOC_ALIGN_ORDER_DEFAULT ilog2(ARCH_KMALLOC_MINALIGN)


/*
 * A pool can be set either for rare-write or read-only mode.
 * In both cases, it can be managed either manually or automatically.
 */
#define PMALLOC_RO		0x00
#define PMALLOC_RW		0x01
#define PMALLOC_AUTO		0x02
#define PMALLOC_MANUAL_RO	PMALLOC_RO
#define PMALLOC_MANUAL_RW	PMALLOC_RW
#define PMALLOC_AUTO_RO		(PMALLOC_RO | PMALLOC_AUTO)
#define PMALLOC_AUTO_RW		(PMALLOC_RW | PMALLOC_AUTO)

#define VM_PMALLOC_PROTECTED_MASK (VM_PMALLOC | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_REWRITABLE_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE)
#define VM_PMALLOC_PROTECTED_REWRITABLE_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE | VM_PMALLOC_PROTECTED)
#define VM_PMALLOC_MASK \
	(VM_PMALLOC | VM_PMALLOC_REWRITABLE | VM_PMALLOC_PROTECTED)


struct pmalloc_pool {
	struct mutex mutex;
	struct list_head pool_node;
	struct llist_head vm_areas;
	size_t refill;
	size_t offset;
	size_t align;
	uint8_t mode;
};


/*
 * Helper functions, not part of the API.
 * They are implemented as inlined functions, instead of macros, for
 * additional type-checking, however they are not meant to be called
 * directly by pmalloc users.
 */
static __always_inline unsigned long __area_flags(struct vmap_area *area)
{
	return area->vm->flags & VM_PMALLOC_MASK;
}

static __always_inline
void __tag_area(struct vmap_area *area, uint32_t mask)
{
	area->vm->flags |= mask;
}

static __always_inline void __untag_area(struct vmap_area *area)
{
	area->vm->flags &= ~VM_PMALLOC_MASK;
}

static __always_inline
struct vmap_area *__current_area(struct pmalloc_pool *pool)
{
	return llist_entry(pool->vm_areas.first, struct vmap_area,
			   area_list);
}

static __always_inline
bool __area_matches_mask(struct vmap_area *area, unsigned long mask)
{
	return (area->vm->flags & mask) == mask;
}

static __always_inline bool __is_area_protected(struct vmap_area *area)
{
	return __area_matches_mask(area, VM_PMALLOC_PROTECTED_MASK);
}

static __always_inline bool __is_area_rewritable(struct vmap_area *area)
{
	return __area_matches_mask(area, VM_PMALLOC_REWRITABLE_MASK);
}

static __always_inline void __protect_area(struct vmap_area *area)
{
	WARN(__is_area_protected(area),
	     "Attempting to protect already protected area %pK", area);
	set_memory_ro(area->va_start, area->vm->nr_pages);
	area->vm->flags |= VM_PMALLOC_PROTECTED_MASK;
}

static __always_inline void __make_area_ro(struct vmap_area *area)
{
	area->vm->flags &= ~VM_PMALLOC_REWRITABLE;
	__protect_area(area);
}

static __always_inline void __unprotect_area(struct vmap_area *area)
{
	WARN(!__is_area_protected(area),
	     "Attempting to unprotect already unprotected area %pK", area);
	set_memory_rw(area->va_start, area->vm->nr_pages);
	__untag_area(area);
}

static __always_inline void __destroy_area(struct vmap_area *area)
{
	WARN(!__is_area_protected(area), "Destroying unprotected area.");
	__unprotect_area(area);
	vfree((void *)area->va_start);
}

static __always_inline bool __empty(struct pmalloc_pool *pool)
{
	return unlikely(llist_empty(&pool->vm_areas));
}

static __always_inline bool __protected(struct pmalloc_pool *pool)
{
	return __is_area_protected(__current_area(pool));
}

static __always_inline bool __unwritable(struct pmalloc_pool *pool)
{
	return __area_flags(__current_area(pool)) == VM_PMALLOC_PROTECTED;
}

static inline bool __exhausted(struct pmalloc_pool *pool, size_t size)
{
	size_t space_before;
	size_t space_after;

	space_before = round_down(pool->offset, pool->align);
	space_after = pool->offset - space_before;
	return unlikely(space_after < size && space_before < size);
}

static __always_inline
bool __space_needed(struct pmalloc_pool *pool, size_t size)
{
	return __empty(pool) || __unwritable(pool) || __exhausted(pool, size);
}

static __always_inline size_t __get_area_pages_size(struct vmap_area *area)
{
	return area->vm->nr_pages * PAGE_SIZE;
}

static __always_inline size_t __get_area_pages_end(struct vmap_area *area)
{
	return area->va_start + __get_area_pages_size(area);
}

static __always_inline
bool __area_contains_range(struct vmap_area *area, const void *addr,
			   size_t n_bytes)
{
	size_t area_end = __get_area_pages_end(area);
	size_t range_start = (size_t)addr;
	size_t range_end = range_start + n_bytes;

	return (area->va_start <= range_start) &&
	       (range_start < area_end) &&
	       (area->va_start <= range_end) &&
	       (range_end <= area_end);
}

static __always_inline
struct vm_struct *__area_from_range(const void *ptr,
				    const unsigned long n_bytes)
{
	struct vm_struct *area;

	if (unlikely(!is_vmalloc_addr(ptr)))
		return NULL;

	area = vmalloc_to_page(ptr)->area;
	if (unlikely(!(area->flags & VM_PMALLOC)))
		return NULL;
	/*
	 * Cannot use __area_contains_range() because it wants a
	 * vmap_area pointer, but because of the way vmalloc works, it's
	 * possible to have only a vm_struct pointer.
	 */
	if (unlikely(!__area_contains_range(area, ptr, n_bytes)))
		return NULL;
	return area;
}

static __always_inline
struct vmap_area *__get_area_from_pool_range(struct pmalloc_pool *pool,
					     const void *addr,
					     size_t n_bytes)
{
	struct vmap_area *area;
	struct vmap_area *cursor;

	if (unlikely(!is_vmalloc_addr(ptr)))
		return NULL;

	area = vmalloc_to_page(ptr)->area;
	if (unlikely(!(area->flags & VM_PMALLOC)))
		return NULL;

	if (unlikely(!__area_contains_range(area, ptr, n_bytes)))
		return NULL;
	return area;

	llist_for_each_entry(cursor, pool->vm_areas.first, area_list)
		if (unlikely(cursor == area))
			return area;
	return NULL;
}

/*
 * Pmalloc API
 */
void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len);

static __always_inline
struct vm_struct *__pmalloc_get_area(const void *ptr,
				     const unsigned long n_bytes)
{
	struct vm_struct *area;
	size_t area_end;
	size_t range_start;
	size_t range_end;

	if (likely(!is_vmalloc_addr(ptr)))
		return NULL;

	area = vmalloc_to_page(ptr)->area;
	if (likely(!(area->flags & VM_PMALLOC)))
		return NULL;

	area_end = __get_area_pages_end(area);
	range_start = (size_t)ptr;
	range_end = range_start + n_bytes;

	return (area->va_start <= range_start) &&
	       (range_start < area_end) &&
	       (area->va_start <= range_end) &&
	       (range_end <= area_end);

	if (!__area_contains_range(area, ptr, n_bytes))
		return (void *)0xBAD;
	return area;
}

/**
 * check_pmalloc_object - helper for hardened usercopy
 * @ptr: the beginning of the memory to check
 * @n: the size of the memory to check
 * @to_user: copy to userspace or from userspace
 *
 * If the check is ok, it will fall-through, otherwise it will abort.
 * The function is inlined, to minimize the performance impact of the
 * extra check to perform on a typically hot path.
 * Micro benchmarking with QEMU shows a reduction of the time spent in this
 * fragment by 60%, when inlined.
 */
static __always_inline
void check_pmalloc_object(const void *ptr, unsigned long n, bool to_user)
{
	

	area = __pmalloc_get_area(ptr, n);
	if (unlikely(area)) {
		if (unlikely(!to_user))
			usercopy_abort("pmalloc",
				       "writing to pmalloc object", to_user,
				       (const unsigned long)ptr, n);
		if (retv < 0)
			usercopy_abort("pmalloc",
				       "invalid pmalloc object", to_user,
				       (const unsigned long)ptr, n);
	}
}

void pmalloc_init_custom_pool(struct pmalloc_pool *pool, size_t refill,
			      unsigned short align_order, uint8_t mode);

struct pmalloc_pool *pmalloc_create_custom_pool(size_t refill,
						unsigned short align_order,
						uint8_t mode);

/**
 * pmalloc_create_pool() - create a protectable memory pool
 * @mode: can the data be altered after protection
 *
 * Shorthand for pmalloc_create_custom_pool() with default argument:
 * * refill is set to PMALLOC_REFILL_DEFAULT
 * * align_order is set to PMALLOC_ALIGN_ORDER_DEFAULT
 *
 * Return:
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

	if (likely(ptr))
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
	if (unlikely(size != 0) && unlikely(n > SIZE_MAX / size))
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
	if (unlikely(size != 0) && unlikely(n > SIZE_MAX / size))
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
	if (likely(buf))
		strncpy(buf, s, len);
	return buf;
}

/*
 * The following sanitation is meant to make life harder for attempts at
 * using ROP/JOP to call this function against areas that are not supposed
 * to be modifiable.
 */
static __always_inline
bool __check_rare_write(struct pmalloc_pool *pool, const void *dst,
			const void *src, size_t n_bytes)
{
	struct vmap_area *area;

	area = pmalloc_get_area(pool, dst, n_bytes);
	return likely(area && __is_area_rewritable(area));

}

static __always_inline
bool __pmalloc_rare_write(struct pmalloc_pool *pool, const void *dst,
			  const void *src, size_t n_bytes)
{
	if (WARN(!__check_rare_write(pool, dst, src, n_bytes),
		 "Incorrect destination."))
		return false;
	return __raw_rare_write(dst, src, RARE_WRITE_VMALLOC_ADDR, n_bytes);
}

static __always_inline
bool pmalloc_rare_write_char(struct pmalloc_pool *pool, const char *dst,
			     const char val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_short(struct pmalloc_pool *pool, const short *dst,
			      const short val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_ushort(struct pmalloc_pool *pool,
			       const unsigned short *dst,
			       const unsigned short val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_int(struct pmalloc_pool *pool, const int *dst,
			    const int val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_uint(struct pmalloc_pool *pool,
			     const unsigned int *dst,
			     const unsigned int val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_long(struct pmalloc_pool *pool, const long *dst,
			     const long val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_ulong(struct pmalloc_pool *pool,
			      const unsigned long *dst,
			      const unsigned long val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_longlong(struct pmalloc_pool *pool,
				 const long long *dst,
				 const long long val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_ulonglong(struct pmalloc_pool *pool,
				  const unsigned long long *dst,
				  const unsigned long long val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

static __always_inline
bool pmalloc_rare_write_ptr(struct pmalloc_pool *pool,
				  const void **dst,
				  const void *val)
{
	return __pmalloc_rare_write(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_rare_write_array() - alters the content of a rewritable pool
 * @pool: the pool associated to the memory to write-protect
 * @dst: where to write the new data
 * @src: the location of the data to replicate into the pool
 * @n_bytes: the size of the region to modify
 *
 * The rare-write functionality is fully implemented as __always_inline,
 * to prevent having an internal function call that is capable of modifying
 * write protected memory.
 * Fully inlining the function allows the compiler to optimize away its
 * interface, making it harder for an attacker to hijack it.
 * This still leaves the door open to attacks that might try to reuse part
 * of the code, by jumping in the middle of the function, however it can
 * be mitigated by having a compiler plugin that enforces Control Flow
 * Integrity (CFI).
 * Any addition/modification to the rare-write path must follow the same
 * approach.

 * Return:
 * * true	- success
 * * false	- error
 */
static __always_inline
bool pmalloc_rare_write_array(struct pmalloc_pool *pool, const void *dst,
			      const void *src, size_t n_bytes)
{
	return __pmalloc_rare_write(pool, dst, src, n_bytes);
}


void pmalloc_protect_pool(struct pmalloc_pool *pool);

void pmalloc_make_pool_ro(struct pmalloc_pool *pool);

void pmalloc_destroy_pool(struct pmalloc_pool *pool);
#endif
#endif
