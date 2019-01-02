/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMALLOC_H
#define _LINUX_VMALLOC_H

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/llist.h>
#include <asm/page.h>		/* pgprot_t */
#include <linux/rbtree.h>
#include <linux/overflow.h>

struct vm_area_struct;		/* vma defining user mapping in mm_types.h */
struct notifier_block;		/* in notifier.h */

/* bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		0x00000001	/* ioremap() and friends */
#define VM_ALLOC		0x00000002	/* vmalloc() */
#define VM_MAP			0x00000004	/* vmap()ed pages */
#define VM_USERMAP		0x00000008	/* suitable for remap_vmalloc_range */
#define VM_UNINITIALIZED	0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		0x00000040      /* don't add guard page */
#define VM_KASAN		0x00000080      /* has allocated kasan shadow memory */
/* bits [20..32] reserved for arch specific ioremap internals */

/*
 * Maximum alignment for ioremap() regions.
 * Can be overriden by arch-specific value.
 */
#ifndef IOREMAP_MAX_ORDER
#define IOREMAP_MAX_ORDER	(7 + PAGE_SHIFT)	/* 128 pages */
#endif

struct vm_struct {
	struct vm_struct	*next;
	void			*addr;
	unsigned long		size;
	unsigned long		flags;
	struct page		**pages;
	unsigned int		nr_pages;
	phys_addr_t		phys_addr;
	const void		*caller;
};

struct vmap_area {
	unsigned long va_start;
	unsigned long va_end;
	unsigned long flags;
	struct rb_node rb_node;         /* address sorted rbtree */
	struct list_head list;          /* address sorted list */
	struct llist_node purge_list;    /* "lazy purge" list */
	struct vm_struct *vm;
	struct rcu_head rcu_head;
};

/*
 *	Highlevel APIs for driver use
 */
extern void vm_unmap_ram(const void *mem, unsigned int count);
extern void *vm_map_ram(struct page **pages, unsigned int count,
				int node, pgprot_t prot);
extern void vm_unmap_aliases(void);

#ifdef CONFIG_MMU
extern void __init vmalloc_init(void);
#else
static inline void vmalloc_init(void)
{
}
#endif

extern void *vmalloc(unsigned long size);
extern void *vzalloc(unsigned long size);
extern void *vmalloc_user(unsigned long size);
extern void *vmalloc_node(unsigned long size, int node);
extern void *vzalloc_node(unsigned long size, int node);
extern void *vmalloc_exec(unsigned long size);
extern void *vmalloc_32(unsigned long size);
extern void *vmalloc_32_user(unsigned long size);
extern void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot);
extern void *__vmalloc_node_range(unsigned long size, unsigned long align,
			unsigned long start, unsigned long end, gfp_t gfp_mask,
			pgprot_t prot, unsigned long vm_flags, int node,
			const void *caller);
#ifndef CONFIG_MMU
extern void *__vmalloc_node_flags(unsigned long size, int node, gfp_t flags);
static inline void *__vmalloc_node_flags_caller(unsigned long size, int node,
						gfp_t flags, void *caller)
{
	return __vmalloc_node_flags(size, node, flags);
}
#else
extern void *__vmalloc_node_flags_caller(unsigned long size,
					 int node, gfp_t flags, void *caller);
#endif

extern void vfree(const void *addr);
extern void vfree_atomic(const void *addr);

extern void *vmap(struct page **pages, unsigned int count,
			unsigned long flags, pgprot_t prot);
extern void vunmap(const void *addr);

extern int remap_vmalloc_range_partial(struct vm_area_struct *vma,
				       unsigned long uaddr, void *kaddr,
				       unsigned long size);

extern int remap_vmalloc_range(struct vm_area_struct *vma, void *addr,
							unsigned long pgoff);
void vmalloc_sync_all(void);
 
/*
 *	Lowlevel-APIs (not for driver use!)
 */

static inline size_t get_vm_area_size(const struct vm_struct *area)
{
	if (!(area->flags & VM_NO_GUARD))
		/* return actual size without guard page */
		return area->size - PAGE_SIZE;
	else
		return area->size;

}

extern struct vm_struct *get_vm_area(unsigned long size, unsigned long flags);
extern struct vm_struct *get_vm_area_caller(unsigned long size,
					unsigned long flags, const void *caller);
extern struct vm_struct *__get_vm_area(unsigned long size, unsigned long flags,
					unsigned long start, unsigned long end);
extern struct vm_struct *__get_vm_area_caller(unsigned long size,
					unsigned long flags,
					unsigned long start, unsigned long end,
					const void *caller);
extern struct vm_struct *remove_vm_area(const void *addr);
extern struct vm_struct *find_vm_area(const void *addr);

extern int map_vm_area(struct vm_struct *area, pgprot_t prot,
			struct page **pages);
#ifdef CONFIG_MMU
extern int map_kernel_range_noflush(unsigned long start, unsigned long size,
				    pgprot_t prot, struct page **pages);
extern void unmap_kernel_range_noflush(unsigned long addr, unsigned long size);
extern void unmap_kernel_range(unsigned long addr, unsigned long size);
#else
static inline int
map_kernel_range_noflush(unsigned long start, unsigned long size,
			pgprot_t prot, struct page **pages)
{
	return size >> PAGE_SHIFT;
}
static inline void
unmap_kernel_range_noflush(unsigned long addr, unsigned long size)
{
}
static inline void
unmap_kernel_range(unsigned long addr, unsigned long size)
{
}
#endif

/* Allocate/destroy a 'vmalloc' VM area. */
extern struct vm_struct *alloc_vm_area(size_t size, pte_t **ptes);
extern void free_vm_area(struct vm_struct *area);

/* for /dev/kmem */
extern long vread(char *buf, char *addr, unsigned long count);
extern long vwrite(char *buf, char *addr, unsigned long count);

/*
 *	Internals.  Dont't use..
 */
extern struct list_head vmap_area_list;
extern __init void vm_area_add_early(struct vm_struct *vm);
extern __init void vm_area_register_early(struct vm_struct *vm, size_t align);

#ifdef CONFIG_SMP
# ifdef CONFIG_MMU
struct vm_struct **pcpu_get_vm_areas(const unsigned long *offsets,
				     const size_t *sizes, int nr_vms,
				     size_t align);

void pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms);
# else
static inline struct vm_struct **
pcpu_get_vm_areas(const unsigned long *offsets,
		const size_t *sizes, int nr_vms,
		size_t align)
{
	return NULL;
}

static inline void
pcpu_free_vm_areas(struct vm_struct **vms, int nr_vms)
{
}
# endif
#endif


/*
 * The range of addresses for VMALLOC begins with the range reserved for
 * VMALLOC_PRMEM allocations, followed by regular read/write allocations:
 *
 *	-------------	VMALLOC_START
 *	VMALLOC_PRMEM
 *	-------------	VMALLOC_RW_START
 *	VMALLOC_RW
 *	-------------	VMALLOC_END
 *
 *	VMALLOC_PRMEM is further subdivided into:
 *	RO_NO_DESTROY: read-only allocations which become permanent till
 *		       reboot, once they are write protected
 *	RO_OK_DESTROY: read-only allocations which can be released, when
 *		       their pool is destroyed.
 *	WR_OK_DESTROY: write rare allocations which can be released, when
 *		       their pool is destroyed.
 *	WR_NO_DESTROY: write rare allocations which cannot be released,
 *		       till reboot.
 *
 *
 *	---------------------	VMALLOC_RO_NO_DESTROY_START
 *	VMALLOC_RO_NO_DESTROY
 *	---------------------	VMALLOC_RO_OK_DESTROY_START
 *	VMALLOC_RO_OK_DESTROY
 *	---------------------	VMALLOC_WR_OK_DESTROY_START
 *	VMALLOC_WR_OK_DESTROY
 *	---------------------	VMALLOC_WR_NO_DESTROY_START
 *	VMALLOC_WR_NO_DESTROY
 *	---------------------	VMALLOC_RW_START
 *
 * Such partitioning allows for inexpensive vetting of address ranges,
 * when performing operations that might be exploited during an attack,
 * such as modifying WR memory or destroying a destroyable allocation
 * pool. Without this vetting, an ROP attack could attemtp to either alter
 * data which is RO, or to destroy a pool that is not meant to be
 * destroyed. This is particularly relevant when used in conjuntion with
 * compilers providing CFI, so that the attack must rely primarily on
 * invoking existing functions with maliciously crafted parameters.
 * XXX NOTE: should the above explanation go elsewhere?
 */

/*
 * XXX NOTE: the ranges below should be reworked, to be eventually
 * compatible also with 32 bit systems, where vmalloc address space is far
 * smaller. They will not cause problems as long as PRMEM is disabled.
 * In practice the amount of data to protect should amount to few tens of
 * megabytes. A major problem could be the IMA measurement list, because
 * it grows indefinitely, but even without PRMEM, that *will* create an
 * upper bound to uptime. A reasonable value for VMALLOC_WR_NO_DESTROY_SIZE
 * should be determined.
 */

#ifdef CONFIG_PRMEM
#define MB _BITUL(20)
#define VMALLOC_RO_NO_DESTROY_SIZE round_up(128 * MB, PAGE_SIZE)
#define VMALLOC_RO_OK_DESTROY_SIZE round_up(128 * MB, PAGE_SIZE)
#define VMALLOC_WR_OK_DESTROY_SIZE round_up(128 * MB, PAGE_SIZE)
#define VMALLOC_WR_NO_DESTROY_SIZE round_up(128 * MB, PAGE_SIZE)
#else
#define VMALLOC_RO_NO_DESTROY_SIZE 0UL
#define VMALLOC_RO_OK_DESTROY_SIZE 0UL
#define VMALLOC_WR_OK_DESTROY_SIZE 0UL
#define VMALLOC_WR_NO_DESTROY_SIZE 0UL
#endif

#define VMALLOC_SIZE (VMALLOC_END - VMALLOC_START)
#define VMALLOC_PRMEM_SIZE \
	(VMALLOC_RO_NO_DESTROY_SIZE + VMALLOC_RO_OK_DESTROY_SIZE + \
	 VMALLOC_WR_NO_DESTROY_SIZE + VMALLOC_WR_OK_DESTROY_SIZE)
#define VMALLOC_RW_SIZE \
	(VMALLOC_SIZE - VMALLOC_PRMEM_SIZE)

#define VMALLOC_RO_NO_DESTROY_START VMALLOC_START
#define VMALLOC_RO_NO_DESTROY_END	\
	(VMALLOC_RO_NO_DESTROY_START + VMALLOC_RO_NO_DESTROY_SIZE)

#define VMALLOC_RO_OK_DESTROY_START VMALLOC_RO_NO_DESTROY_END
#define VMALLOC_RO_OK_DESTROY_END	\
	(VMALLOC_RO_OK_DESTROY_START + VMALLOC_RO_OK_DESTROY_SIZE)

#define VMALLOC_WR_OK_DESTROY_START VMALLOC_RO_OK_DESTROY_END
#define VMALLOC_WR_OK_DESTROY_END \
	(VMALLOC_WR_OK_DESTROY_START + VMALLOC_WR_OK_DESTROY_SIZE)

#define VMALLOC_WR_NO_DESTROY_START VMALLOC_WR_OK_DESTROY_END
#define VMALLOC_WR_NO_DESTROY_END \
	(VMALLOC_WR_NO_DESTROY_START + VMALLOC_WR_NO_DESTROY_SIZE)

#define VMALLOC_RW_START VMALLOC_WR_NO_DESTROY_END
#define VMALLOC_RW_END VMALLOC_END

#define is_vmalloc_ro_no_destroy(addr)		\
({						\
	unsigned long p = (unsigned long)addr;	\
						\
	((VMALLOC_RO_NO_DESTROY_START <= p) &&	\
	 (VMALLOC_RO_NO_DESTROY_END > p));	\
})

#define is_vmalloc_ro_ok_destroy(addr)		\
({						\
	unsigned long p = (unsigned long)addr;	\
						\
	((VMALLOC_RO_OK_DESTROY_START <= p) &&	\
	 (VMALLOC_RO_OK_DESTROY_END > p));	\
})

#define is_vmalloc_wr_ok_destroy(addr)		\
({						\
	unsigned long p = (unsigned long)addr;	\
						\
	((VMALLOC_WR_OK_DESTROY_START <= p) &&	\
	 (VMALLOC_WR_NO_DESTROY_END > p));	\
})

#define is_vmalloc_wr_no_destroy(addr)		\
({						\
	unsigned long p = (unsigned long)addr;	\
						\
	((VMALLOC_WR_NO_DESTROY_START <= p) &&	\
	 (VMALLOC_WR_NO_DESTROY_END > p));		\
})

#define is_vmalloc_ro(addr)						\
({									\
	unsigned long p = (unsigned long)addr;				\
									\
	(is_vmalloc_ro_ok_destroy(p) || is_vmalloc_ro_no_destroy(p));	\
})

#define is_vmalloc_wr(addr)						\
({									\
	unsigned long p = (unsigned long)addr;				\
									\
	(is_vmalloc_wr_ok_destroy(p) || is_vmalloc_wr_no_destroy(p));	\
})

#define is_vmalloc_ok_destroy(addr)					\
({									\
	unsigned long p = (unsigned long)addr;				\
									\
	is_vmalloc_ro_ok_destroy(p) || is_vmalloc_wr_ok_destroy(p);		\
})

#define is_vmalloc_no_destroy(addr)					\
({									\
	unsigned long p = (unsigned long)addr;				\
									\
	(is_vmalloc_ro_no_destroy(p) || is_vmalloc_wr_no_destroy(p));	\
})

int register_vmap_purge_notifier(struct notifier_block *nb);
int unregister_vmap_purge_notifier(struct notifier_block *nb);

#endif /* _LINUX_VMALLOC_H */
