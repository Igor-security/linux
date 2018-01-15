// SPDX-License-Identifier: GPL-2.0
/*
 * Basic general purpose allocator for managing special purpose
 * memory, for example, memory that is not managed by the regular
 * kmalloc/kfree interface.  Uses for this includes on-device special
 * memory, uncached memory etc.
 *
 * It is safe to use the allocator in NMI handlers and other special
 * unblockable contexts that could otherwise deadlock on locks.  This
 * is implemented by using atomic operations and retries on any
 * conflicts.  The disadvantage is that there may be livelocks in
 * extreme cases.  For better scalability, one allocator can be used
 * for each CPU.
 *
 * The lockless operation only works if there is enough memory
 * available.  If new memory is added to the pool a lock has to be
 * still taken.  So any user relying on locklessness has to ensure
 * that sufficient memory is preallocated.
 *
 * The basic atomic operation of this allocator is cmpxchg on long.
 * On architectures that don't have NMI-safe cmpxchg implementation,
 * the allocator can NOT be used in NMI handler.  So code uses the
 * allocator in NMI handler should depend on
 * CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 *
 * Copyright 2005 (C) Jes Sorensen <jes@trained-monkey.org>
 *
 *
 * Encoding of the bitmap tracking the allocations
 * -----------------------------------------------
 *
 * The bitmap is composed of units of allocations.
 *
 * Each unit of allocation is represented using 2 consecutive bits.
 *
 * This makes it possible to encode, for each unit of allocation,
 * information about:
 *  - allocation status (busy/free)
 *  - beginning of a sequennce of allocation units (first / successive)
 *
 *
 * Dictionary of allocation units (msb to the left, lsb to the right):
 *
 * 11: first allocation unit in the allocation
 * 10: any subsequent allocation unit (if any) in the allocation
 * 00: available allocation unit
 * 01: invalid
 *
 * Example, using the same notation as above - MSb.......LSb:
 *
 *  ...000010111100000010101011   <-- Read in this direction.
 *     \__|\__|\|\____|\______|
 *        |   | |     |       \___ 4 used allocation units
 *        |   | |     \___________ 3 empty allocation units
 *        |   | \_________________ 1 used allocation unit
 *        |   \___________________ 2 used allocation units
 *        \_______________________ 2 empty allocation units
 *
 * The encoding allows for lockless operations, such as:
 * - search for a sufficiently large range of allocation units
 * - reservation of a selected range of allocation units
 * - release of a specific allocation
 *
 * The alignment at which to perform the search for sequence of empty
 * allocation units (marked as zeros in the bitmap) is 2^1.
 *
 * This means that an allocation can start only at even places
 * (bit 0, bit 2, etc.) in the bitmap.
 *
 * Therefore, the number of zeroes to look for must be twice the number
 * of desired allocation units.
 *
 * When it's time to free the memory associated to an allocation request,
 * it's a matter of checking if the corresponding allocation unit is
 * really the beginning of an allocation (both bits are set to 1).
 *
 * Looking for the ending can also be performed locklessly.
 * It's sufficient to identify the first mapped allocation unit
 * that is represented either as free (00) or busy (11).
 * Even if the allocation status should change in the meanwhile, it
 * doesn't matter, since it can only transition between free (00) and
 * first-allocated (11).
 *
 * The parameter indicating to the *_free() function the size of the
 * space that should be freed can be either set to 0, for automated
 * assessment, or it can be specified explicitly.
 *
 * In case it is specified explicitly, the value is verified agaisnt what
 * the library is tracking internally.
 *
 * If ever needed, the bitmap could be extended, assigning larger amounts
 * of bits to each allocation unit (the increase must follow powers of 2),
 * to track other properties of the allocations.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/bitmap.h>
#include <linux/rculist.h>
#include <linux/interrupt.h>
#include <linux/genalloc.h>
#include <linux/of_device.h>
#include <linux/bug.h>

#define ENTRY_ORDER 1UL
#define ENTRY_MASK ((1UL << ((ENTRY_ORDER) + 1UL)) - 1UL)
#define ENTRY_HEAD ENTRY_MASK
#define ENTRY_UNUSED 0UL
#define BITS_PER_ENTRY (1U << ENTRY_ORDER)
#define BITS_DIV_ENTRIES(x) ((x) >> ENTRY_ORDER)
#define ENTRIES_TO_BITS(x) ((x) << ENTRY_ORDER)
#define BITS_DIV_LONGS(x) ((x) / BITS_PER_LONG)
#define ENTRIES_DIV_LONGS(x) (BITS_DIV_LONGS(ENTRIES_TO_BITS(x)))

#define ENTRIES_PER_LONG BITS_DIV_ENTRIES(BITS_PER_LONG)

/* Binary pattern of 1010...1010 that spans one unsigned long. */
#define MASK (~0UL / 3 * 2)

/**
 * get_bitmap_entry() - extracts the specified entry from the bitmap
 * @map: pointer to a bitmap
 * @entry_index: the index of the desired entry in the bitmap
 *
 * Return: The requested bitmap entry.
 */
static inline unsigned long get_bitmap_entry(unsigned long *map,
					    int entry_index)
{
	return (map[ENTRIES_DIV_LONGS(entry_index)] >>
		ENTRIES_TO_BITS(entry_index % ENTRIES_PER_LONG)) &
		ENTRY_MASK;
}


/**
 * mem_to_units() - convert references to memory into orders of allocation
 * @size: amount in bytes
 * @order: power of 2 represented by each entry in the bitmap
 *
 * Return: the number of units representing the size.
 */
static inline unsigned long mem_to_units(unsigned long size,
					 unsigned long order)
{
	return (size + (1UL << order) - 1) >> order;
}

/**
 * chunk_size() - dimension of a chunk of memory, in bytes
 * @chunk: pointer to the struct describing the chunk
 *
 * Return: The size of the chunk, in bytes.
 */
static inline size_t chunk_size(const struct gen_pool_chunk *chunk)
{
	return chunk->end_addr - chunk->start_addr + 1;
}


/**
 * set_bits_ll() - based on value and mask, sets bits at address
 * @addr: where to write
 * @mask: filter to apply for the bits to alter
 * @value: actual configuration of bits to store
 *
 * Return:
 * * 0		- success
 * * -EBUSY	- otherwise
 */
static int set_bits_ll(unsigned long *addr,
		       unsigned long mask, unsigned long value)
{
	unsigned long nval;
	unsigned long present;
	unsigned long target;

	nval = *addr;
	do {
		present = nval;
		if (present & mask)
			return -EBUSY;
		target =  present | value;
		cpu_relax();
	} while ((nval = cmpxchg(addr, present, target)) != target);
	return 0;
}


/**
 * clear_bits_ll() - based on value and mask, clears bits at address
 * @addr: where to write
 * @mask: filter to apply for the bits to alter
 * @value: actual configuration of bits to clear
 *
 * Return:
 * * 0		- success
 * * -EBUSY	- otherwise
 */
static int clear_bits_ll(unsigned long *addr,
			 unsigned long mask, unsigned long value)
{
	unsigned long nval;
	unsigned long present;
	unsigned long target;

	nval = *addr;
	present = nval;
	if (unlikely((present & mask) ^ value))
		return -EBUSY;
	do {
		present = nval;
		if (unlikely((present & mask) ^ value))
			return -EBUSY;
		target =  present & ~mask;
		cpu_relax();
	} while ((nval = cmpxchg(addr, present, target)) != target);
	return 0;
}


/**
 * get_length() - length of the allocation beginning at start_entry index
 * @map: pointer to a bitmap
 * @start_entry: the index of the first entry in the bitmap
 * @chunk_entries: number of entries in the chunk
 *
 * Return:
 * * length of an allocation	- success
 * * 0				- invalid parameters or bitmap
 */
static unsigned int get_length(unsigned long *map,
			       unsigned int start_entry,
			       unsigned int chunk_entries)
{
	int i;
	unsigned long bitmap_entry;


	if (unlikely(get_bitmap_entry(map, start_entry) != ENTRY_HEAD))
		return 0;
	for (i = start_entry + 1; i < chunk_entries; i++) {
		bitmap_entry = get_bitmap_entry(map, i);
		if (bitmap_entry == ENTRY_HEAD ||
		    bitmap_entry == ENTRY_UNUSED)
			break;
	}
	return i - start_entry;
}


/*
 * alter_bitmap_ll() - set/clear the entries associated with an allocation
 * @alteration: indicates if the bits selected should be set or cleared
 * @map: pointer to a bitmap
 * @start: the index of the first entry in the bitmap
 * @nentries: number of entries to alter
 *
 * The modification happens lock-lessly.
 * Several users can write to the same map simultaneously, without lock.
 * In case of mid-air conflict, when 2 or more writers try to alter the
 * same word in the bitmap, only one will succeed and continue, the others
 * will fail and receive as return value the amount of entries that were
 * not written. Each failed writer is responsible to revert the changes
 * it did to the bitmap.
 * The lockless conflict resolution is implemented through cmpxchg.
 * Success or failure is purely based on first come first served basis.
 * The first writer that manages to gain write access to the target word
 * of the bitmap wins. Whatever can affect the order and priority of execution
 * of the writers can and will affect the result of the race.
 *
 * Return:
 * * 0			- success
 * * remaining entries	- failure
 */
static unsigned int alter_bitmap_ll(int (*action)(unsigned long *addr,
						  unsigned long mask,
						  unsigned long value),
				    unsigned long *map,
				    unsigned int start_entry,
				    unsigned int nentries)
{
	unsigned long start_bit;
	unsigned long end_bit;
	unsigned long mask;
	unsigned long value;
	unsigned int nbits;
	unsigned int bits_to_write;
	unsigned int index;

	/*
	 * Prepare for writing the initial part of the allocation, from
	 * starting entry, to the end of the UL bitmap element which
	 * contains it. It might be larger than the actual allocation.
	 */
	start_bit = ENTRIES_TO_BITS(start_entry);
	end_bit = ENTRIES_TO_BITS(start_entry + nentries);
	nbits = ENTRIES_TO_BITS(nentries);
	bits_to_write = BITS_PER_LONG - start_bit % BITS_PER_LONG;
	mask = BITMAP_FIRST_WORD_MASK(start_bit);
	/* Mark the beginning of the allocation. */
	value = MASK | (1UL << (start_bit % BITS_PER_LONG));
	index = BITS_DIV_LONGS(start_bit);

	/*
	 * Writes entries to the bitmap, as long as the reminder is
	 * positive or zero.
	 * Might be skipped if the entries to write do not reach the end
	 * of a bitmap UL unit.
	 */
	while (nbits >= bits_to_write) {
		if (action(map + index, mask, value & mask))
			return BITS_DIV_ENTRIES(nbits);
		nbits -= bits_to_write;
		bits_to_write = BITS_PER_LONG;
		mask = ~0UL;
		value = MASK;
		index++;
	}

	/* Takes care of the ending part of the entries to mark. */
	if (nbits > 0) {
		mask ^= BITMAP_FIRST_WORD_MASK((end_bit) % BITS_PER_LONG);
		bits_to_write = nbits;
		if (action(map + index, mask, value & mask))
			return BITS_DIV_ENTRIES(nbits);
	}
	return 0;
}

static inline unsigned int set_bitmap_ll(unsigned long *map,
					 unsigned int start_entry,
					 unsigned int nentries)
{
	return alter_bitmap_ll(set_bits_ll, map, start_entry, nentries);
}

static inline unsigned int clear_bitmap_ll(unsigned long *map,
					   unsigned int start_entry,
					   unsigned int nentries)
{
	return alter_bitmap_ll(clear_bits_ll, map, start_entry, nentries);
}

/**
 * gen_pool_create() - create a new special memory pool
 * @min_alloc_order: log base 2 of number of bytes each bitmap entry
 *		     represents
 * @nid: node id of the node the pool structure should be allocated on,
 *	 or -1
 *
 * Create a new special memory pool that can be used to manage special
 * purpose memory not managed by the regular kmalloc/kfree interface.
 *
 * Return:
 * * pointer to the pool	- success
 * * NULL			- otherwise
 */
struct gen_pool *gen_pool_create(int min_alloc_order, int nid)
{
	struct gen_pool *pool;

	pool = kmalloc_node(sizeof(struct gen_pool), GFP_KERNEL, nid);
	if (pool != NULL) {
		spin_lock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->chunks);
		pool->min_alloc_order = min_alloc_order;
		pool->algo = gen_pool_first_fit;
		pool->data = NULL;
		pool->name = NULL;
	}
	return pool;
}
EXPORT_SYMBOL(gen_pool_create);

/**
 * gen_pool_add_virt() - add a new chunk of special memory to the pool
 * @pool: pool to add new memory chunk to
 * @virt: virtual starting address of memory chunk to add to pool
 * @phys: physical starting address of memory chunk to add to pool
 * @size: size in bytes of the memory chunk to add to pool
 * @nid: node id of the node the chunk structure and bitmap should be
 *       allocated on, or -1
 *
 * Add a new chunk of special memory to the specified pool.
 *
 * Return:
 * * 0		- success
 * * -ve errno	- failure
 */
int gen_pool_add_virt(struct gen_pool *pool, unsigned long virt,
		      phys_addr_t phys, size_t size, int nid)
{
	struct gen_pool_chunk *chunk;
	unsigned int nentries;
	unsigned int nbytes;

	nentries = size >> pool->min_alloc_order;
	nbytes = sizeof(struct gen_pool_chunk) +
		 ENTRIES_DIV_LONGS(nentries) * sizeof(long);
	chunk = kzalloc_node(nbytes, GFP_KERNEL, nid);
	if (unlikely(chunk == NULL))
		return -ENOMEM;

	chunk->phys_addr = phys;
	chunk->start_addr = virt;
	chunk->end_addr = virt + size - 1;
	atomic_long_set(&chunk->avail, size);

	spin_lock(&pool->lock);
	list_add_rcu(&chunk->next_chunk, &pool->chunks);
	spin_unlock(&pool->lock);

	return 0;
}
EXPORT_SYMBOL(gen_pool_add_virt);

/**
 * gen_pool_virt_to_phys() - return the physical address of memory
 * @pool: pool to allocate from
 * @addr: starting address of memory
 *
 * Return:
 * * the physical address	- success
 * * \-1			- error
 */
phys_addr_t gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long addr)
{
	struct gen_pool_chunk *chunk;
	phys_addr_t paddr = -1;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (addr >= chunk->start_addr && addr <= chunk->end_addr) {
			paddr = chunk->phys_addr + (addr - chunk->start_addr);
			break;
		}
	}
	rcu_read_unlock();

	return paddr;
}
EXPORT_SYMBOL(gen_pool_virt_to_phys);

/**
 * gen_pool_destroy() - destroy a special memory pool
 * @pool: pool to destroy
 *
 * Destroy the specified special memory pool. Verifies that there are no
 * outstanding allocations.
 */
void gen_pool_destroy(struct gen_pool *pool)
{
	struct list_head *_chunk, *_next_chunk;
	struct gen_pool_chunk *chunk;
	unsigned int order = pool->min_alloc_order;
	unsigned long bit, end_bit;
	bool empty = true;

	list_for_each_safe(_chunk, _next_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);
		list_del(&chunk->next_chunk);

		end_bit = chunk_size(chunk) >> order;
		bit = find_next_bit(chunk->entries, end_bit, 0);
		if (WARN(bit < end_bit,
			 "Attempt to destroy non-empty pool %s",
			 pool->name)) {
			empty = false;
			continue;
		}
		kfree(chunk);
	}
	if (likely(empty)) {
		kfree_const(pool->name);
		kfree(pool);
	}
}
EXPORT_SYMBOL(gen_pool_destroy);

/**
 * gen_pool_alloc() - get memory from the pool
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the pool allocation function (with first-fit algorithm by default).
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 *
 * Return:
 * * address of the memory allocated	- success
 * * NULL				- error
 */
unsigned long gen_pool_alloc(struct gen_pool *pool, size_t size)
{
	return gen_pool_alloc_algo(pool, size, pool->algo, pool->data);
}
EXPORT_SYMBOL(gen_pool_alloc);

/**
 * gen_pool_alloc_algo() - get memory from pool with specified algorythm
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 * @algo: algorithm passed from caller
 * @data: data passed to algorithm
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the provided @algo function to find room for the allocation.
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 *
 * Return:
 * * address of the memory allocated	- success
 * * NULL				- error
 */
unsigned long gen_pool_alloc_algo(struct gen_pool *pool, size_t size,
		genpool_algo_t algo, void *data)
{
	struct gen_pool_chunk *chunk;
	unsigned long addr = 0;
	unsigned int start_entry;
	unsigned int end_entry;
	unsigned int nentries;
	unsigned int remain;
	unsigned int order = pool->min_alloc_order;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	BUG_ON(in_nmi());
#endif

	if (size == 0)
		return 0;

	nentries = mem_to_units(size, order);
	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (size > atomic_long_read(&chunk->avail))
			continue;

		start_entry = 0;
		end_entry = chunk_size(chunk) >> order;
retry:
		start_entry = algo(chunk->entries, end_entry, start_entry,
				  nentries, data, pool);
		if (start_entry >= end_entry)
			continue;
		remain = set_bitmap_ll(chunk->entries, start_entry, nentries);
		if (remain) {
			remain = clear_bitmap_ll(chunk->entries,
						 start_entry,
						 nentries - remain);
			goto retry;
		}

		addr = chunk->start_addr +
			((unsigned long)start_entry << order);
		size = nentries << order;
		atomic_long_sub(size, &chunk->avail);
		break;
	}
	rcu_read_unlock();
	return addr;
}
EXPORT_SYMBOL(gen_pool_alloc_algo);

/**
 * gen_pool_dma_alloc() - allocate special memory from the pool for DMA usage
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 * @dma: dma-view physical address return value.  Use NULL if unneeded.
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the pool allocation function (with first-fit algorithm by default).
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 *
 * Return:
 * * address of the memory allocated	- success
 * * NULL				- error
 */
void *gen_pool_dma_alloc(struct gen_pool *pool, size_t size, dma_addr_t *dma)
{
	unsigned long vaddr;

	vaddr = gen_pool_alloc(pool, size);
	if (!vaddr)
		return NULL;

	if (dma)
		*dma = gen_pool_virt_to_phys(pool, vaddr);

	return (void *)vaddr;
}
EXPORT_SYMBOL(gen_pool_dma_alloc);

static void chunk_free_allocation(struct gen_pool *pool,
				  struct gen_pool_chunk *chunk,
				  unsigned long addr, size_t size)
{
	unsigned int length;
	unsigned int start_entry;
	unsigned int chunk_entries;
	unsigned int unprocessed;
	unsigned int order = pool->min_alloc_order;

	if (WARN(addr + size - 1 > chunk->end_addr,
		 "Trying to free unallocated memory from pool %s",
		 pool->name))
		return;

	chunk_entries = (chunk->end_addr - chunk->start_addr + 1) >> order;
	start_entry = (addr - chunk->start_addr) >> order;
	length = get_length(chunk->entries, start_entry, chunk_entries);
	if (WARN(length == 0,
		 "Corrupted pool %s", pool->name))
		return;

	if (WARN(size && (length != mem_to_units(size, order)),
		 "Size provided and size measured in pool %s differ",
		 pool->name))
		return;

	unprocessed = clear_bitmap_ll(chunk->entries, start_entry, length);
	if (WARN(unprocessed, "bitmap collision freeing memory in pool %s",
		 pool->name))
		return;

	atomic_long_add(length << order, &chunk->avail);
}

/**
 * gen_pool_free() - free allocated special memory back to the pool
 * @pool: pool to free to
 * @addr: starting address of memory to free back to pool
 * @size: size in bytes of memory to free or 0, for auto-detection
 *
 * Free previously allocated special memory back to the specified
 * pool.  Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 */
void gen_pool_free(struct gen_pool *pool, unsigned long addr, size_t size)
{
	struct gen_pool_chunk *chunk;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	BUG_ON(in_nmi());
#endif

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (addr >= chunk->start_addr && addr <= chunk->end_addr) {
			chunk_free_allocation(pool, chunk, addr, size);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	WARN(true, "address not found in pool %s", pool->name);
}
EXPORT_SYMBOL(gen_pool_free);


/**
 * gen_pool_flush_chunk() - drops all the allocations from a specific chunk
 * @pool:	the generic memory pool
 * @chunk:	The chunk to wipe clear.
 *
 * This is meant to be called only while destroying a pool. It's up to the
 * caller to avoid races, but really, at this point the pool should have
 * already been retired and it should have become unavailable for any other
 * sort of operation.
 */
void gen_pool_flush_chunk(struct gen_pool *pool,
			  struct gen_pool_chunk *chunk)
{
	size_t size;

	size = chunk->end_addr + 1 - chunk->start_addr;
	memset(chunk->entries, 0,
	       DIV_ROUND_UP(size >> pool->min_alloc_order * BITS_PER_ENTRY,
			    BITS_PER_BYTE));
	atomic_long_set(&chunk->avail, size);
}


/**
 * gen_pool_for_each_chunk() - call func for every chunk of generic memory pool
 * @pool:	the generic memory pool
 * @func:	func to call
 * @data:	additional data used by @func
 *
 * Call @func for every chunk of generic memory pool.  The @func is
 * called with rcu_read_lock held.
 */
void gen_pool_for_each_chunk(struct gen_pool *pool,
	void (*func)(struct gen_pool *pool, struct gen_pool_chunk *chunk,
		     void *data), void *data)
{
	struct gen_pool_chunk *chunk;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &(pool)->chunks, next_chunk)
		func(pool, chunk, data);
	rcu_read_unlock();
}
EXPORT_SYMBOL(gen_pool_for_each_chunk);

/**
 * addr_in_gen_pool() - checks if an address falls within the range of a pool
 * @pool:	the generic memory pool
 * @start:	start address
 * @size:	size of the region
 *
 * Check if the range of addresses falls within the specified pool.
 *
 * Return:
 * * true	- the entire range is contained in the pool
 * * false	- otherwise
 */
bool addr_in_gen_pool(struct gen_pool *pool, unsigned long start,
		      size_t size)
{
	bool found = false;
	unsigned long end = start + size - 1;
	struct gen_pool_chunk *chunk;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &(pool)->chunks, next_chunk) {
		if (start >= chunk->start_addr && start <= chunk->end_addr) {
			if (end <= chunk->end_addr) {
				found = true;
				break;
			}
		}
	}
	rcu_read_unlock();
	return found;
}

/**
 * gen_pool_avail() - get available free space of the pool
 * @pool: pool to get available free space
 *
 * Return: available free space of the specified pool.
 */
size_t gen_pool_avail(struct gen_pool *pool)
{
	struct gen_pool_chunk *chunk;
	size_t avail = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk)
		avail += atomic_long_read(&chunk->avail);
	rcu_read_unlock();
	return avail;
}
EXPORT_SYMBOL_GPL(gen_pool_avail);

/**
 * gen_pool_size() - get size in bytes of memory managed by the pool
 * @pool: pool to get size
 *
 * Return: size in bytes of memory managed by the pool.
 */
size_t gen_pool_size(struct gen_pool *pool)
{
	struct gen_pool_chunk *chunk;
	size_t size = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk)
		size += chunk_size(chunk);
	rcu_read_unlock();
	return size;
}
EXPORT_SYMBOL_GPL(gen_pool_size);

/**
 * gen_pool_set_algo() - set the allocation algorithm
 * @pool: pool to change allocation algorithm
 * @algo: custom algorithm function
 * @data: additional data used by @algo
 *
 * Call @algo for each memory allocation in the pool.
 * If @algo is NULL use gen_pool_first_fit as default
 * memory allocation function.
 */
void gen_pool_set_algo(struct gen_pool *pool, genpool_algo_t algo, void *data)
{
	rcu_read_lock();

	pool->algo = algo;
	if (!pool->algo)
		pool->algo = gen_pool_first_fit;

	pool->data = data;

	rcu_read_unlock();
}
EXPORT_SYMBOL(gen_pool_set_algo);

/**
 * gen_pool_first_fit() - find the first available region
 * of memory matching the size requirement (no alignment constraint)
 * @map: The address to base the search on
 * @size: The number of allocation units in the bitmap
 * @start: The allocation unit to start searching at
 * @nr: The number of allocation units we're looking for
 * @data: additional data - unused
 * @pool: pool to find the fit region memory from
 *
 * Return:
 * * index of the memory allocated	- sufficient space available
 * * end of the range			- insufficient space
 */
unsigned long gen_pool_first_fit(unsigned long *map, unsigned long size,
				 unsigned long start, unsigned int nr,
				 void *data, struct gen_pool *pool)
{
	unsigned long align_mask;
	unsigned long bit_index;

	align_mask = roundup_pow_of_two(BITS_PER_ENTRY) - 1;
	bit_index = bitmap_find_next_zero_area(map, ENTRIES_TO_BITS(size),
					       ENTRIES_TO_BITS(start),
					       ENTRIES_TO_BITS(nr),
					       align_mask);
	return BITS_DIV_ENTRIES(bit_index);
}
EXPORT_SYMBOL(gen_pool_first_fit);

/**
 * gen_pool_first_fit_align() - find the first available region
 * of memory matching the size requirement (alignment constraint)
 * @map: The address to base the search on
 * @size: The number of allocation units in the bitmap
 * @start: The allocation unit to start searching at
 * @nr: The number of allocation units we're looking for
 * @data: data for alignment
 * @pool: pool to get order from
 *
 * Return:
 * * index of the memory allocated	- sufficient space available
 * * end of the range			- insufficient space
 */
unsigned long gen_pool_first_fit_align(unsigned long *map,
				       unsigned long size,
				       unsigned long start,
				       unsigned int nr, void *data,
				       struct gen_pool *pool)
{
	struct genpool_data_align *alignment;
	unsigned long align_mask;
	unsigned long bit_index;
	unsigned int order;

	alignment = data;
	order = pool->min_alloc_order;
	align_mask = roundup_pow_of_two(
			ENTRIES_TO_BITS(mem_to_units(alignment->align,
						     order))) - 1;
	bit_index = bitmap_find_next_zero_area(map, ENTRIES_TO_BITS(size),
					       ENTRIES_TO_BITS(start),
					       ENTRIES_TO_BITS(nr),
					       align_mask);
	return BITS_DIV_ENTRIES(bit_index);
}
EXPORT_SYMBOL(gen_pool_first_fit_align);

/**
 * gen_pool_fixed_alloc() - reserve a specific region
 * @map: The address to base the search on
 * @size: The number of allocation units in the bitmap
 * @start: The allocation unit to start searching at
 * @nr: The number of allocation units we're looking for
 * @data: data for alignment
 * @pool: pool to get order from
 *
 * Return:
 * * index of the memory allocated	- sufficient space available
 * * end of the range			- insufficient space
 */
unsigned long gen_pool_fixed_alloc(unsigned long *map, unsigned long size,
				   unsigned long start, unsigned int nr,
				   void *data, struct gen_pool *pool)
{
	struct genpool_data_fixed *fixed_data;
	unsigned int order;
	unsigned long offset;
	unsigned long align_mask;
	unsigned long bit_index;

	fixed_data = data;
	order = pool->min_alloc_order;
	if (WARN_ON(fixed_data->offset & ((1UL << order) - 1)))
		return size;
	offset = fixed_data->offset >> order;
	align_mask = roundup_pow_of_two(BITS_PER_ENTRY) - 1;
	bit_index = bitmap_find_next_zero_area(map, ENTRIES_TO_BITS(size),
					       ENTRIES_TO_BITS(start + offset),
					       ENTRIES_TO_BITS(nr), align_mask);
	if (bit_index != ENTRIES_TO_BITS(offset))
		return size;

	return BITS_DIV_ENTRIES(bit_index);
}
EXPORT_SYMBOL(gen_pool_fixed_alloc);

/**
 * gen_pool_first_fit_order_align() - find the first available region
 * of memory matching the size requirement. The region will be aligned
 * to the order of the size specified.
 * @map: The address to base the search on
 * @size: The number of allocation units in the bitmap
 * @start: The allocation unit to start searching at
 * @nr: The number of allocation units we're looking for
 * @data: additional data - unused
 * @pool: pool to find the fit region memory from
 *
 * Return:
 * * index of the memory allocated	- sufficient space available
 * * end of the range			- insufficient space
 */
unsigned long gen_pool_first_fit_order_align(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr, void *data,
					     struct gen_pool *pool)
{
	unsigned long align_mask;
	unsigned long bit_index;

	align_mask = roundup_pow_of_two(ENTRIES_TO_BITS(nr)) - 1;
	bit_index = bitmap_find_next_zero_area(map, ENTRIES_TO_BITS(size),
					       ENTRIES_TO_BITS(start),
					       ENTRIES_TO_BITS(nr),
					       align_mask);
	return BITS_DIV_ENTRIES(bit_index);
}
EXPORT_SYMBOL(gen_pool_first_fit_order_align);

/**
 * gen_pool_best_fit() - find the best fitting region of memory
 * matching the size requirement (no alignment constraint)
 * @map: The address to base the search on
 * @size: The number of allocation units in the bitmap
 * @start: The allocation unit to start searching at
 * @nr: The number of allocation units we're looking for
 * @data: additional data - unused
 * @pool: pool to find the fit region memory from
 *
 * Iterate over the bitmap to find the smallest free region
 * which we can allocate the memory.
 *
 * Return:
 * * index of the memory allocated	- sufficient space available
 * * end of the range			- insufficient space
 */
unsigned long gen_pool_best_fit(unsigned long *map, unsigned long size,
				unsigned long start, unsigned int nr,
				void *data, struct gen_pool *pool)
{
	unsigned long start_bit = ENTRIES_TO_BITS(size);
	unsigned long len = size + 1;
	unsigned long index;
	unsigned long align_mask;
	unsigned long bit_index;

	align_mask = roundup_pow_of_two(BITS_PER_ENTRY) - 1;
	bit_index = bitmap_find_next_zero_area(map, ENTRIES_TO_BITS(size),
					       ENTRIES_TO_BITS(start),
					       ENTRIES_TO_BITS(nr),
					       align_mask);
	index = BITS_DIV_ENTRIES(bit_index);

	while (index < size) {
		unsigned long next_bit;

		next_bit = find_next_bit(map, ENTRIES_TO_BITS(size),
					 ENTRIES_TO_BITS(index + nr));
		if ((BITS_DIV_ENTRIES(next_bit) - index) < len) {
			len = BITS_DIV_ENTRIES(next_bit) - index;
			start_bit = ENTRIES_TO_BITS(index);
			if (len == nr)
				return BITS_DIV_ENTRIES(start_bit);
		}
		bit_index =
			bitmap_find_next_zero_area(map,
						   ENTRIES_TO_BITS(size),
						   next_bit + 1,
						   ENTRIES_TO_BITS(nr),
						   align_mask);
		index = BITS_DIV_ENTRIES(bit_index);
	}

	return BITS_DIV_ENTRIES(start_bit);
}
EXPORT_SYMBOL(gen_pool_best_fit);

static void devm_gen_pool_release(struct device *dev, void *res)
{
	gen_pool_destroy(*(struct gen_pool **)res);
}

static int devm_gen_pool_match(struct device *dev, void *res, void *data)
{
	struct gen_pool **p = res;

	/* NULL data matches only a pool without an assigned name */
	if (!data && !(*p)->name)
		return 1;

	if (!data || !(*p)->name)
		return 0;

	return !strcmp((*p)->name, data);
}

/**
 * gen_pool_get() - Obtain the gen_pool (if any) for a device
 * @dev: device to retrieve the gen_pool from
 * @name: name of a gen_pool or NULL, identifies a particular gen_pool
 *	  on device
 *
 * Return:
 * * the gen_pool for the device	- if it exists
 * * NULL				- no pool exists for the device
 */
struct gen_pool *gen_pool_get(struct device *dev, const char *name)
{
	struct gen_pool **p;

	p = devres_find(dev, devm_gen_pool_release, devm_gen_pool_match,
			(void *)name);
	if (!p)
		return NULL;
	return *p;
}
EXPORT_SYMBOL_GPL(gen_pool_get);

/**
 * devm_gen_pool_create() - managed gen_pool_create
 * @dev: device that provides the gen_pool
 * @min_alloc_order: log base 2 of number of bytes each bitmap bit represents
 * @nid: node selector for allocated gen_pool, %NUMA_NO_NODE for all nodes
 * @name: name of a gen_pool or NULL, identifies a particular gen_pool on device
 *
 * Create a new special memory pool that can be used to manage special purpose
 * memory not managed by the regular kmalloc/kfree interface. The pool will be
 * automatically destroyed by the device management code.
 *
 * Return:
 * * address of the pool	- success
 * * NULL			- error
 */
struct gen_pool *devm_gen_pool_create(struct device *dev, int min_alloc_order,
				      int nid, const char *name)
{
	struct gen_pool **ptr, *pool;
	const char *pool_name = NULL;

	/* Check that genpool to be created is uniquely addressed on device */
	if (gen_pool_get(dev, name))
		return ERR_PTR(-EINVAL);

	if (name) {
		pool_name = kstrdup_const(name, GFP_KERNEL);
		if (!pool_name)
			return ERR_PTR(-ENOMEM);
	}

	ptr = devres_alloc(devm_gen_pool_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		goto free_pool_name;

	pool = gen_pool_create(min_alloc_order, nid);
	if (!pool)
		goto free_devres;

	*ptr = pool;
	pool->name = pool_name;
	devres_add(dev, ptr);

	return pool;

free_devres:
	devres_free(ptr);
free_pool_name:
	kfree_const(pool_name);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(devm_gen_pool_create);

#ifdef CONFIG_OF
/**
 * of_gen_pool_get() - find a pool by phandle property
 * @np: device node
 * @propname: property name containing phandle(s)
 * @index: index into the phandle array
 *
 * Return:
 * * pool address	- it contains the chunk starting at the physical
 *			  address of the device tree node pointed at by
 *			  the phandle property
 * * NULL		- otherwise
 */
struct gen_pool *of_gen_pool_get(struct device_node *np,
				 const char *propname, int index)
{
	struct platform_device *pdev;
	struct device_node *np_pool, *parent;
	const char *name = NULL;
	struct gen_pool *pool = NULL;

	np_pool = of_parse_phandle(np, propname, index);
	if (!np_pool)
		return NULL;

	pdev = of_find_device_by_node(np_pool);
	if (!pdev) {
		/* Check if named gen_pool is created by parent node device */
		parent = of_get_parent(np_pool);
		pdev = of_find_device_by_node(parent);
		of_node_put(parent);

		of_property_read_string(np_pool, "label", &name);
		if (!name)
			name = np_pool->name;
	}
	if (pdev)
		pool = gen_pool_get(&pdev->dev, name);
	of_node_put(np_pool);

	return pool;
}
EXPORT_SYMBOL_GPL(of_gen_pool_get);
#endif /* CONFIG_OF */
