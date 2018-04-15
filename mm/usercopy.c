/*
 * This implements the various checks for CONFIG_HARDENED_USERCOPY*,
 * which are designed to protect kernel memory from needless exposure
 * and overwrite under many unintended conditions. This code is based
 * on PAX_USERCOPY, which is:
 *
 * Copyright (C) 2001-2016 PaX Team, Bradley Spengler, Open Source
 * Security Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/thread_info.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/pmalloc.h>
#include <linux/sched/clock.h>
#include <asm/sections.h>

/*
 * Checks if a given pointer and length is contained by the current
 * stack frame (if possible).
 *
 * Returns:
 *	NOT_STACK: not at all on the stack
 *	GOOD_FRAME: fully within a valid stack frame
 *	GOOD_STACK: fully on the stack (when can't do frame-checking)
 *	BAD_STACK: error condition (invalid stack position or bad stack frame)
 */
static noinline int check_stack_object(const void *obj, unsigned long len)
{
	const void * const stack = task_stack_page(current);
	const void * const stackend = stack + THREAD_SIZE;
	int ret;

	/* Object is not on the stack at all. */
	if (obj + len <= stack || stackend <= obj)
		return NOT_STACK;

	/*
	 * Reject: object partially overlaps the stack (passing the
	 * the check above means at least one end is within the stack,
	 * so if this check fails, the other end is outside the stack).
	 */
	if (obj < stack || stackend < obj + len)
		return BAD_STACK;

	/* Check if object is safely within a valid frame. */
	ret = arch_within_stack_frames(stack, stackend, obj, len);
	if (ret)
		return ret;

	return GOOD_STACK;
}

/*
 * If these functions are reached, then CONFIG_HARDENED_USERCOPY has found
 * an unexpected state during a copy_from_user() or copy_to_user() call.
 * There are several checks being performed on the buffer by the
 * __check_object_size() function. Normal stack buffer usage should never
 * trip the checks, and kernel text addressing will always trip the check.
 * For cache objects, it is checking that only the whitelisted range of
 * bytes for a given cache is being accessed (via the cache's usersize and
 * useroffset fields). To adjust a cache whitelist, use the usercopy-aware
 * kmem_cache_create_usercopy() function to create the cache (and
 * carefully audit the whitelist range).
 */
void usercopy_warn(const char *name, const char *detail, bool to_user,
		   unsigned long offset, unsigned long len)
{
	WARN_ONCE(1, "Bad or missing usercopy whitelist? Kernel memory %s attempt detected %s %s%s%s%s (offset %lu, size %lu)!\n",
		 to_user ? "exposure" : "overwrite",
		 to_user ? "from" : "to",
		 name ? : "unknown?!",
		 detail ? " '" : "", detail ? : "", detail ? "'" : "",
		 offset, len);
}

void __noreturn usercopy_abort(const char *name, const char *detail,
			       bool to_user, unsigned long offset,
			       unsigned long len)
{
	pr_emerg("Kernel memory %s attempt detected %s %s%s%s%s (offset %lu, size %lu)!\n",
		 to_user ? "exposure" : "overwrite",
		 to_user ? "from" : "to",
		 name ? : "unknown?!",
		 detail ? " '" : "", detail ? : "", detail ? "'" : "",
		 offset, len);

	/*
	 * For greater effect, it would be nice to do do_group_exit(),
	 * but BUG() actually hooks all the lock-breaking and per-arch
	 * Oops code, so that is used here instead.
	 */
	BUG();
}

/* Returns true if any portion of [ptr,ptr+n) over laps with [low,high). */
static bool overlaps(const unsigned long ptr, unsigned long n,
		     unsigned long low, unsigned long high)
{
	const unsigned long check_low = ptr;
	unsigned long check_high = check_low + n;

	/* Does not overlap if entirely above or entirely below. */
	if (check_low >= high || check_high <= low)
		return false;

	return true;
}

/* Is this address range in the kernel text area? */
static inline void check_kernel_text_object(const unsigned long ptr,
					    unsigned long n, bool to_user)
{
	unsigned long textlow = (unsigned long)_stext;
	unsigned long texthigh = (unsigned long)_etext;
	unsigned long textlow_linear, texthigh_linear;

	if (overlaps(ptr, n, textlow, texthigh))
		usercopy_abort("kernel text", NULL, to_user, ptr - textlow, n);

	/*
	 * Some architectures have virtual memory mappings with a secondary
	 * mapping of the kernel text, i.e. there is more than one virtual
	 * kernel address that points to the kernel image. It is usually
	 * when there is a separate linear physical memory mapping, in that
	 * __pa() is not just the reverse of __va(). This can be detected
	 * and checked:
	 */
	textlow_linear = (unsigned long)lm_alias(textlow);
	/* No different mapping: we're done. */
	if (textlow_linear == textlow)
		return;

	/* Check the secondary mapping... */
	texthigh_linear = (unsigned long)lm_alias(texthigh);
	if (overlaps(ptr, n, textlow_linear, texthigh_linear))
		usercopy_abort("linear kernel text", NULL, to_user,
			       ptr - textlow_linear, n);
}

static inline void check_bogus_address(const unsigned long ptr, unsigned long n,
				       bool to_user)
{
	/* Reject if object wraps past end of memory. */
	if (ptr + n < ptr)
		usercopy_abort("wrapped address", NULL, to_user, 0, ptr + n);

	/* Reject if NULL or ZERO-allocation. */
	if (ZERO_OR_NULL_PTR(ptr))
		usercopy_abort("null address", NULL, to_user, ptr, n);
}

/* Checks for allocs that are marked in some way as spanning multiple pages. */
static inline void check_page_span(const void *ptr, unsigned long n,
				   struct page *page, bool to_user)
{
#ifdef CONFIG_HARDENED_USERCOPY_PAGESPAN
	const void *end = ptr + n - 1;
	struct page *endpage;
	bool is_reserved, is_cma;

	/*
	 * Sometimes the kernel data regions are not marked Reserved (see
	 * check below). And sometimes [_sdata,_edata) does not cover
	 * rodata and/or bss, so check each range explicitly.
	 */

	/* Allow reads of kernel rodata region (if not marked as Reserved). */
	if (ptr >= (const void *)__start_rodata &&
	    end <= (const void *)__end_rodata) {
		if (!to_user)
			usercopy_abort("rodata", NULL, to_user, 0, n);
		return;
	}

	/* Allow kernel data region (if not marked as Reserved). */
	if (ptr >= (const void *)_sdata && end <= (const void *)_edata)
		return;

	/* Allow kernel bss region (if not marked as Reserved). */
	if (ptr >= (const void *)__bss_start &&
	    end <= (const void *)__bss_stop)
		return;

	/* Is the object wholly within one base page? */
	if (likely(((unsigned long)ptr & (unsigned long)PAGE_MASK) ==
		   ((unsigned long)end & (unsigned long)PAGE_MASK)))
		return;

	/* Allow if fully inside the same compound (__GFP_COMP) page. */
	endpage = virt_to_head_page(end);
	if (likely(endpage == page))
		return;

	/*
	 * Reject if range is entirely either Reserved (i.e. special or
	 * device memory), or CMA. Otherwise, reject since the object spans
	 * several independently allocated pages.
	 */
	is_reserved = PageReserved(page);
	is_cma = is_migrate_cma_page(page);
	if (!is_reserved && !is_cma)
		usercopy_abort("spans multiple pages", NULL, to_user, 0, n);

	for (ptr += PAGE_SIZE; ptr <= end; ptr += PAGE_SIZE) {
		page = virt_to_head_page(ptr);
		if (is_reserved && !PageReserved(page))
			usercopy_abort("spans Reserved and non-Reserved pages",
				       NULL, to_user, 0, n);
		if (is_cma && !is_migrate_cma_page(page))
			usercopy_abort("spans CMA and non-CMA pages", NULL,
				       to_user, 0, n);
	}
#endif
}

static inline void check_heap_object(const void *ptr, unsigned long n,
				     bool to_user)
{
	struct page *page;

	if (!virt_addr_valid(ptr))
		return;

	page = virt_to_head_page(ptr);

	if (PageSlab(page)) {
		/* Check slab allocator for flags and size. */
		__check_heap_object(ptr, n, page, to_user);
	} else {
		/* Verify object does not incorrectly span multiple pages. */
		check_page_span(ptr, n, page, to_user);
	}
}

#ifdef CONFIG_PROTECTABLE_MEMORY

static inline int is_pmalloc_object(const void *ptr, const unsigned long n)
{
	struct vm_struct *area;
	unsigned long start = (unsigned long)ptr;
	unsigned long end = start + n;
	unsigned long area_end;

	if (likely(!is_vmalloc_addr(ptr)))
		return false;

	area = vmalloc_to_page(ptr)->area;
	if (unlikely(!(area->flags & VM_PMALLOC)))
		return false;

	area_end = area->nr_pages * PAGE_SIZE + (unsigned long)area->addr;
	return (start <= end) && (end <= area_end);
}

static inline void check_pmalloc_object(const void *ptr, unsigned long n,
					bool to_user)
{
	int retv;

	retv = is_pmalloc_object(ptr, n);
	if (unlikely(retv)) {
		if (unlikely(!to_user))
			usercopy_abort("pmalloc",
				       "trying to write to pmalloc object",
				       to_user, (const unsigned long)ptr, n);
		if (retv < 0)
			usercopy_abort("pmalloc",
				       "invalid pmalloc object",
				       to_user, (const unsigned long)ptr, n);
	}
}

#else

static inline void check_pmalloc_object(const void *ptr, unsigned long n,
					bool to_user)
{
}
#endif

/*
 * Validates that the given object is:
 * - not bogus address
 * - known-safe heap or stack object
 * - not in kernel text
 */
void __check_object_size(const void *ptr, unsigned long n, bool to_user)
{
	/* Skip all tests if size is zero. */
	if (!n)
		return;

	/* Check for invalid addresses. */
	check_bogus_address((const unsigned long)ptr, n, to_user);

	/* Check for bad heap object. */
	check_heap_object(ptr, n, to_user);

	/* Check for bad stack object. */
	switch (check_stack_object(ptr, n)) {
	case NOT_STACK:
		/* Object is not touching the current process stack. */
		break;
	case GOOD_FRAME:
	case GOOD_STACK:
		/*
		 * Object is either in the correct frame (when it
		 * is possible to check) or just generally on the
		 * process stack (when frame checking not available).
		 */
		return;
	default:
		usercopy_abort("process stack", NULL, to_user, 0, n);
	}

	/* Check for object in kernel to avoid text exposure. */
	check_kernel_text_object((const unsigned long)ptr, n, to_user);

	/* Check if object is from a pmalloc chunk. */
	check_pmalloc_object(ptr, n, to_user);
}
EXPORT_SYMBOL(__check_object_size);

#define AREAS 128
struct area {
	void *address;
	unsigned long long size;
	unsigned long long with;
	unsigned long long without;
};

static struct dentry *dir;
static struct dentry *with;
static struct dentry *without;
static struct dentry *dir;
static unsigned long meas_with = 5;
static unsigned long meas_without = 7;
static struct area *areas;
static struct pmalloc_pool *pool;

static int prepare_debugfs(void)
{
	dir = debugfs_create_dir("measurements", 0);
	if (unlikely(!dir))
		return -1;
	with = debugfs_create_ulong("with", 0666, dir, &meas_with);
	if (unlikely(!with))
		goto error;
	without = debugfs_create_ulong("without", 0666, dir, &meas_without);
	if (unlikely(!without))
		goto error;
	return 0;
error:
	debugfs_remove_recursive(dir);
	return -1;
}

static bool prepare_areas(void)
{
	int i;
	size_t size;

	areas = vmalloc(sizeof(struct area) * AREAS);
	if (unlikely(!areas))
		return false;

	pool = pmalloc_create_pool();
	if (unlikely(!pool)) {
		vfree(areas);
		return false;
	}

	for (i = 0; i < AREAS; i++) {
		size = PAGE_SIZE << (i & 3);
		areas[i].address = pmalloc(pool, size);
		if (unlikely(!areas[i].address)) {
			pmalloc_destroy_pool(pool);
			vfree(areas);
			return false;
		}
		areas[i].size = size;
	}
	return true;
}

static inline void do_measurement_with(int i)
{
	unsigned long long start;
	unsigned long long end;
	bool to_user = 1;
	void *ptr = areas[i].address;
	unsigned long n = areas[i].size - 1;

	start = sched_clock();
	/* Check for invalid addresses. */
	check_bogus_address((const unsigned long)ptr, n, to_user);

	/* Check if object is from a pmalloc chunk. */
	check_pmalloc_object(ptr, n, to_user);

	/* Check for bad heap object. */
	check_heap_object(ptr, n, to_user);

	/* Check for bad stack object. */
	switch (check_stack_object(ptr, n)) {
	case NOT_STACK:
		/* Object is not touching the current process stack. */
		break;
	case GOOD_FRAME:
	case GOOD_STACK:
		/*
		 * Object is either in the correct frame (when it
		 * is possible to check) or just generally on the
		 * process stack (when frame checking not available).
		 */
		return;
	default:
		usercopy_abort("process stack", NULL, to_user, 0, n);
	}

	/* Check for object in kernel to avoid text exposure. */
	check_kernel_text_object((const unsigned long)ptr, n, to_user);

	end = sched_clock();
	areas[i].with = end - start;
}

static inline void do_measurement_without(int i)
{
	unsigned long long start;
	unsigned long long end;
	bool to_user = 1;
	void *ptr = areas[i].address;
	unsigned long n = areas[i].size - 1;

	start = sched_clock();
	/* Check for invalid addresses. */
	check_bogus_address((const unsigned long)ptr, n, to_user);

	/* Check for bad heap object. */
	check_heap_object(ptr, n, to_user);

	/* Check for bad stack object. */
	switch (check_stack_object(ptr, n)) {
	case NOT_STACK:
		/* Object is not touching the current process stack. */
		break;
	case GOOD_FRAME:
	case GOOD_STACK:
		/*
		 * Object is either in the correct frame (when it
		 * is possible to check) or just generally on the
		 * process stack (when frame checking not available).
		 */
		return;
	default:
		usercopy_abort("process stack", NULL, to_user, 0, n);
	}

	/* Check for object in kernel to avoid text exposure. */
	check_kernel_text_object((const unsigned long)ptr, n, to_user);

	end = sched_clock();
	areas[i].without = end - start;
}

static int __init measure_user_copy(void)
{
	int i;
	unsigned long long tmp;

	if (unlikely(prepare_debugfs() ||
	    unlikely(!prepare_areas())))
		return 0;
	for (i = 0; i < AREAS; i++) {
		do_measurement_with(i);
		do_measurement_without(i);
	}
	for (tmp = 0, i = 0; i < AREAS; i++)
		tmp += areas[i].with;
	meas_with = (unsigned long)(tmp / AREAS);
	for (tmp = 0, i = 0; i < AREAS; i++)
		tmp += areas[i].without;
	meas_without = (unsigned long)(tmp / AREAS);
	return 0;
}
core_initcall(measure_user_copy);
