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

extern long __start_rare_write_after_init;
extern long __end_rare_write_after_init;

enum rare_write_type {
	RARE_WRITE_VIRT_ADDR,
	RARE_WRITE_VMALLOC_ADDR,
};

__always_inline bool __rare_write_check_bounds(void *dst)
{
	return (dst >= (void *)&__start_rare_write_after_init) &&
	       (dst < (void *)&__end_rare_write_after_init);
}

/*
 * This is the core of the rare write functionality.
 * It doesn't perform any check on the validity of the target.
 * The wrapper using it is supposed to apply sensible verification
 * criteria, depending on the specific use-case and, for avoiding run-time
 * checks, also specify the type of memory being modified.
 */
__always_inline
void *__raw_rare_write(void *dst, void *src, enum rare_write_type type,
		       size_t n_bytes)
{
	size_t size;

	while (n_bytes) {
		struct page *page;
		void *base;
		unsigned long offset;
		size_t offset_complement;

		if (type == RARE_WRITE_VIRT_ADDR)
			page = virt_to_page(dst);
		else
			page = vmalloc_to_page(dst);
		base = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		if (WARN(!base, "failed to remap rare-write page"))
			return dst;
		offset = (unsigned long)dst & ~PAGE_MASK;
		offset_complement = ((size_t)PAGE_SIZE) - offset;
		size = min(((int)n_bytes), ((int)offset_complement));
		memcpy(base + offset, src, size);
		vunmap(base);
		dst += size;
		src += size;
		n_bytes -= size;
	}
	return dst;
}

__always_inline void *__rare_write(void *dst, void *src, size_t n_bytes)
{

	if (WARN(!(__rare_write_check_bounds(dst)),
		 "Not a valid rare_write destination."))
		return NULL;
	return __raw_rare_write(dst, src, RARE_WRITE_VIRT_ADDR, n_bytes);

}

#define rare_write(dst, src) (*(typeof(dst) *)__rare_write(dst, src, sizeof(*(src))))
#define rare_write_array(dst, src, size) __rare_write(dst, src, size)
#endif


