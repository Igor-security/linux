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

#include <linux/write_rare.h>
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
 * either read-only or it can support write rare.
 *
 * The write rare mechanism is intended to avoid any read overhead and
 * still some form of protection, while a selected area is modified.
 * This will incur into a penalty that is partially depending on the
 * specific architecture, but in general is linearly proportional to the
 * rounded up amount of pages being altered.
 *
 * For additional safety, it is not possible to have in the same pool both
 * write rare and unmodifiable memory.
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
#define PMALLOC_DEFAULT_REFILL_SIZE PAGE_SIZE
#define PMALLOC_ALIGN_ORDER_DEFAULT ilog2(ARCH_KMALLOC_MINALIGN)


/*
 * A pool can be set either for write rare or read-only mode.
 * In both cases, the protection of its content can be managed either
 * manually or automatically.
 *
 * AUTO_RO/AUTO_WR mean that every vmap area in a pool is automatically
 * protected, whenever it becomes full or anyway unsuitable for the next
 * allocation at hand, because there is not enough free space left.
 * Only the latest allocation is guarranteed to be still writable
 * directly, for example with an assignment.
 * Every other, prior, allocation, must be assumed to be already write
 * protected.
 * Thus, allocating a new chunk from a certain pool, forfaits the
 * possibility of writing directly to any previous allocation from that
 * pool.
 * If the pool is in write rare mode, it's still possible to alter its
 * data, as long as it's done through functions of the write rare family.
 * START_WR precludes even that possibility: when the memory is allocated,
 * it is already write protected.
 */
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
	struct llist_head vm_areas;
	size_t align;
	size_t refill;
	size_t offset;
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
	area->vm->flags |= (mask & VM_PMALLOC_MASK);
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
	return (area->vm->flags & VM_PMALLOC_MASK) == mask;
}

static __always_inline bool __is_area_protected(struct vmap_area *area)
{
	return (__area_flags(area) & VM_PMALLOC_PROTECTED_MASK) ==
		VM_PMALLOC_PROTECTED_MASK;
}

static __always_inline bool __is_area_wr(struct vmap_area *area)
{
	return (__area_flags(area) & (VM_PMALLOC | VM_PMALLOC_WR)) ==
		(VM_PMALLOC | VM_PMALLOC_WR);
}

/* The area size backed by pages, without the canary bird. */
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

	return (n_bytes > 0) && (area->va_start <= range_start) &&
	       (range_end <= area_end);
}

static __always_inline
struct vmap_area *__pool_get_area(struct pmalloc_pool *pool,
				  const void *addr, size_t n_bytes)
{
	struct vmap_area *area;

	if (unlikely(!is_vmalloc_addr(addr)))
		return NULL;

	llist_for_each_entry(area, pool->vm_areas.first, area_list)
		if (unlikely(__area_contains_range(area, addr, n_bytes))) {
			if (WARN(!(area->vm->flags & VM_PMALLOC),
				 "area in pool not tagged as VM_PMALLOC"))
				return NULL;
			return area;
		}
	return NULL;
}

static inline bool __is_pmalloc_addr(const void *ptr)
{
	struct vm_struct *area;
	struct page *page;

	if (likely(!is_vmalloc_addr(ptr)))
		return false;

	page = vmalloc_to_page(ptr);
	if (unlikely(!page))
		return false;

	area = page->area;
	if (likely(!(area && (area->flags & VM_PMALLOC))))
		return false;

	return true;
}

/*
 * ---------------------------- Pmalloc API ----------------------------
 */

void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len);

/**
 * check_pmalloc_object - helper for hardened usercopy
 * @ptr: the beginning of the memory to check
 * @n: the size of the memory to check
 * @to_user: copy to userspace or from userspace
 *
 * If the check is ok, it will fall-through, otherwise it will abort.
 * The function is inlined, to minimize the performance impact of the
 * extra check that can end up on a hot path.
 * Non-exhaustive micro benchmarking with QEMU x86_64 shows a reduction of
 * the time spent in this fragment by 60%, when inlined.
 */
static inline
void check_pmalloc_object(const void *ptr, unsigned long n, bool to_user)
{
	if (unlikely(__is_pmalloc_addr(ptr)))
		usercopy_abort("pmalloc", "accessing pmalloc object",
			       to_user, (const unsigned long)ptr, n);
}

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

/*
 * The following sanitation is meant to make life harder for attempts at
 * using ROP/JOP to call this function against areas that are not supposed
 * to be modifiable.
 */
static __always_inline
bool __check_wr(struct pmalloc_pool *pool, const void *dst, size_t n_bytes)
{
	struct vmap_area *area;

	area = __pool_get_area(pool, dst, n_bytes);
	return likely(area && __is_area_wr(area));

}

/**
 * pmalloc_wr_memset() - alters the content of a rewritable pool
 * @pool: the pool associated to the memory to write-protect
 * @dst: where to write the new data
 * @c: the byte to write (repeatedly)
 * @n_bytes: the size of the region to modify
 *
 * Return:
 * * true	- success
 * * false	- error
 */
static __always_inline
bool pmalloc_wr_memset(struct pmalloc_pool *pool, const void *dst,
		       const int c, size_t n_bytes)
{
	if (WARN(!__check_wr(pool, dst, n_bytes), "Incorrect destination"))
		return false;
	return __raw_wr_memset(dst, c, WR_VMALLOC_ADDR, n_bytes);
}

/**
 * pmalloc_wr_copy() - alters the content of a rewritable pool
 * @pool: the pool associated to the memory to write-protect
 * @dst: where to write the new data
 * @src: the location of the data to replicate into the pool
 * @n_bytes: the size of the region to modify

 * Return:
 * * true	- success
 * * false	- error
 */
static __always_inline
bool pmalloc_wr_copy(struct pmalloc_pool *pool, const void *dst,
		      const void *src, size_t n_bytes)
{
	if (WARN(!__check_wr(pool, dst, n_bytes), "Incorrect destination"))
		return false;
	return __raw_wr_copy(dst, src, WR_VMALLOC_ADDR, n_bytes);
}

/**
 * pmalloc_wr_char - alters a variable of type char
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_char(struct pmalloc_pool *pool, const char *dst,
		     const char val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_short - alters a short
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_short(struct pmalloc_pool *pool, const short *dst,
		      const short val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_ushort - alters an unsigned short
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_ushort(struct pmalloc_pool *pool,
		       const unsigned short *dst, const unsigned short val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_int - alters an int
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_int(struct pmalloc_pool *pool, const int *dst,
		    const int val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_uint - alters an unsigned int
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_uint(struct pmalloc_pool *pool, const unsigned int *dst,
		     const unsigned int val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_long - alters a long
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_long(struct pmalloc_pool *pool, const long *dst,
		     const long val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_ulong - alters an unsigned long
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_ulong(struct pmalloc_pool *pool, const unsigned long *dst,
		      const unsigned long val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_longlong - alters a long long
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_longlong(struct pmalloc_pool *pool, const long long *dst,
			 const long long val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_ulonglong - alters an unsigned long long
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_ulonglong(struct pmalloc_pool *pool,
			  const unsigned long long *dst,
			  const unsigned long long val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

/**
 * pmalloc_wr_ptr - alters a pointer
 * @pool: pointer to the pool containing the memory to be written
 * @dst: the address of the memory to be written
 * @val: the value to write
 *
 * Return: true upon success, false otherwise
 */
static __always_inline
bool pmalloc_wr_ptr(struct pmalloc_pool *pool, const void *dst,
		    const void *val)
{
	return pmalloc_wr_copy(pool, dst, &val, sizeof(val));
}

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
		pmalloc_wr_memset(pool, ptr, 0, size);
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
		pmalloc_wr_copy(pool, buf, s, len);
	else
		strncpy(buf, s, len);
	return buf;
}


void pmalloc_protect_pool(struct pmalloc_pool *pool);

void pmalloc_make_pool_ro(struct pmalloc_pool *pool);

bool pmalloc_is_address_protected(void *p);

void pmalloc_destroy_pool(struct pmalloc_pool *pool);
#endif
#endif
