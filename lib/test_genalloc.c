// SPDX-License-Identifier: GPL-2.0
/*
 * test_genalloc.c
 *
 * (C) Copyright 2017 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <linux/genalloc.h>

#include <linux/test_genalloc.h>


/*
 * In case of failure of any of these tests, memory corruption is almost
 * guarranteed; allowing the boot to continue means risking to corrupt
 * also any filesystem/block device accessed write mode.
 * Therefore, BUG_ON() is used, when testing.
 */


/*
 * Keep the bitmap small, while including case of cross-ulong mapping.
 * For simplicity, the test cases use only 1 chunk of memory.
 */
#define BITMAP_SIZE_C 16
#define ALLOC_ORDER 0

#define ULONG_SIZE (sizeof(unsigned long))
#define BITMAP_SIZE_UL (BITMAP_SIZE_C / ULONG_SIZE)
#define MIN_ALLOC_SIZE (1 << ALLOC_ORDER)
#define ENTRIES (BITMAP_SIZE_C * 8)
#define CHUNK_SIZE  (MIN_ALLOC_SIZE * ENTRIES)

#ifndef CONFIG_TEST_GENERIC_ALLOCATOR_VERBOSE

static inline void print_first_chunk_bitmap(struct gen_pool *pool) {}

#else

static void print_first_chunk_bitmap(struct gen_pool *pool)
{
	struct gen_pool_chunk *chunk;
	char bitmap[BITMAP_SIZE_C * 2 + 1];
	unsigned long i;
	char *bm = bitmap;
	char *entry;

	if (unlikely(pool == NULL || pool->chunks.next == NULL))
		return;

	chunk = container_of(pool->chunks.next, struct gen_pool_chunk,
			     next_chunk);
	entry = (void *)chunk->entries;
	for (i = 1; i <= BITMAP_SIZE_C; i++)
		bm += snprintf(bm, 3, "%02hhx", entry[BITMAP_SIZE_C - i]);
	*bm = '\0';
	pr_notice("chunk: %p    bitmap: 0x%s\n", chunk, bitmap);

}

#endif

enum test_commands {
	CMD_ALLOCATOR,
	CMD_ALLOCATE,
	CMD_FLUSH,
	CMD_FREE,
	CMD_NUMBER,
	CMD_END = CMD_NUMBER,
};

struct null_struct {
	void *null;
};

struct test_allocator {
	genpool_algo_t algo;
	union {
		struct genpool_data_align align;
		struct genpool_data_fixed offset;
		struct null_struct null;
	} data;
};

struct test_action {
	unsigned int location;
	char pattern[BITMAP_SIZE_C];
	unsigned int size;
};


struct test_command {
	enum test_commands command;
	union {
		struct test_allocator allocator;
		struct test_action action;
	};
};


/*
 * To pass an array literal as parameter to a macro, it must go through
 * this one, first.
 */
#define ARR(...) __VA_ARGS__

#define SET_DATA(parameter, value)	\
	.parameter = {			\
		.parameter = value,	\
	}				\

#define SET_ALLOCATOR(alloc, parameter, value)		\
{							\
	.command = CMD_ALLOCATOR,			\
	.allocator = {					\
		.algo = (alloc),			\
		.data = {				\
			SET_DATA(parameter, value),	\
		},					\
	}						\
}

#define ACTION_MEM(act, mem_size, mem_loc, match)	\
{							\
	.command = act,					\
	.action = {					\
		.size = (mem_size),			\
		.location = (mem_loc),			\
		.pattern = match,			\
	},						\
}

#define ALLOCATE_MEM(mem_size, mem_loc, match)	\
	ACTION_MEM(CMD_ALLOCATE, mem_size, mem_loc, ARR(match))

#define FREE_MEM(mem_size, mem_loc, match)	\
	ACTION_MEM(CMD_FREE, mem_size, mem_loc, ARR(match))

#define FLUSH_MEM()		\
{				\
	.command = CMD_FLUSH,	\
}

#define END()			\
{				\
	.command = CMD_END,	\
}

static inline int compare_bitmaps(const struct gen_pool *pool,
				   const char *reference)
{
	struct gen_pool_chunk *chunk;
	char *bitmap;
	unsigned int i;

	chunk = container_of(pool->chunks.next, struct gen_pool_chunk,
			     next_chunk);
	bitmap = (char *)chunk->entries;

	for (i = 0; i < BITMAP_SIZE_C; i++)
		if (bitmap[i] != reference[i])
			return -1;
	return 0;
}

static void callback_set_allocator(struct gen_pool *pool,
				   const struct test_command *cmd,
				   unsigned long *locations)
{
	gen_pool_set_algo(pool, cmd->allocator.algo,
			  (void *)&cmd->allocator.data);
}

static void callback_allocate(struct gen_pool *pool,
			      const struct test_command *cmd,
			      unsigned long *locations)
{
	const struct test_action *action = &cmd->action;

	locations[action->location] = gen_pool_alloc(pool, action->size);
	BUG_ON(!locations[action->location]);
	print_first_chunk_bitmap(pool);
	BUG_ON(compare_bitmaps(pool, action->pattern));
}

static void callback_flush(struct gen_pool *pool,
			  const struct test_command *cmd,
			  unsigned long *locations)
{
	unsigned int i;

	for (i = 0; i < ENTRIES; i++)
		if (locations[i]) {
			gen_pool_free(pool, locations[i], 0);
			locations[i] = 0;
		}
}

static void callback_free(struct gen_pool *pool,
			  const struct test_command *cmd,
			  unsigned long *locations)
{
	const struct test_action *action = &cmd->action;

	gen_pool_free(pool, locations[action->location], 0);
	locations[action->location] = 0;
	print_first_chunk_bitmap(pool);
	BUG_ON(compare_bitmaps(pool, action->pattern));
}

static void (* const callbacks[CMD_NUMBER])(struct gen_pool *,
					    const struct test_command *,
					    unsigned long *) = {
	[CMD_ALLOCATOR] = callback_set_allocator,
	[CMD_ALLOCATE] = callback_allocate,
	[CMD_FREE] = callback_free,
	[CMD_FLUSH] = callback_flush,
};

static const struct test_command test_first_fit[] = {
	SET_ALLOCATOR(gen_pool_first_fit, null, NULL),
	ALLOCATE_MEM(3, 0, ARR({0x2b})),
	ALLOCATE_MEM(2, 1, ARR({0xeb, 0x02})),
	ALLOCATE_MEM(5, 2, ARR({0xeb, 0xae, 0x0a})),
	FREE_MEM(2, 1,  ARR({0x2b, 0xac, 0x0a})),
	ALLOCATE_MEM(1, 1, ARR({0xeb, 0xac, 0x0a})),
	FREE_MEM(0, 2,  ARR({0xeb})),
	FREE_MEM(0, 0,  ARR({0xc0})),
	FREE_MEM(0, 1,	ARR({0x00})),
	END(),
};

/*
 * To make the test work for both 32bit and 64bit ulong sizes,
 * allocate (8 / 2 * 4 - 1) = 15 bytes bytes, then 16, then 2.
 * The first allocation prepares for the crossing of the 32bit ulong
 * threshold. The following crosses the 32bit threshold and prepares for
 * crossing the 64bit thresholds. The last is large enough (2 bytes) to
 * cross the 64bit threshold.
 * Then free the allocations in the order: 2nd, 1st, 3rd.
 */
static const struct test_command test_ulong_span[] = {
	SET_ALLOCATOR(gen_pool_first_fit, null, NULL),
	ALLOCATE_MEM(15, 0, ARR({0xab, 0xaa, 0xaa, 0x2a})),
	ALLOCATE_MEM(16, 1, ARR({0xab, 0xaa, 0xaa, 0xea,
				0xaa, 0xaa, 0xaa, 0x2a})),
	ALLOCATE_MEM(2, 2, ARR({0xab, 0xaa, 0xaa, 0xea,
			       0xaa, 0xaa, 0xaa, 0xea,
			       0x02})),
	FREE_MEM(0, 1, ARR({0xab, 0xaa, 0xaa, 0x2a,
			   0x00, 0x00, 0x00, 0xc0,
			   0x02})),
	FREE_MEM(0, 0, ARR({0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00, 0x00, 0xc0,
			   0x02})),
	FREE_MEM(0, 2, ARR({0x00})),
	END(),
};

/*
 * Create progressively smaller allocations A B C D E.
 * then free B and D.
 * Then create new allocation that would fit in both of the gaps left by
 * B and D. Verify that it uses the gap from B.
 */
static const struct test_command test_first_fit_gaps[] = {
	SET_ALLOCATOR(gen_pool_first_fit, null, NULL),
	ALLOCATE_MEM(10, 0, ARR({0xab, 0xaa, 0x0a})),
	ALLOCATE_MEM(8, 1, ARR({0xab, 0xaa, 0xba, 0xaa,
			       0x0a})),
	ALLOCATE_MEM(6, 2, ARR({0xab, 0xaa, 0xba, 0xaa,
			       0xba, 0xaa})),
	ALLOCATE_MEM(4, 3, ARR({0xab, 0xaa, 0xba, 0xaa,
			       0xba, 0xaa, 0xab})),
	ALLOCATE_MEM(2, 4, ARR({0xab, 0xaa, 0xba, 0xaa,
			       0xba, 0xaa, 0xab, 0x0b})),
	FREE_MEM(0, 1, ARR({0xab, 0xaa, 0x0a, 0x00,
			   0xb0, 0xaa, 0xab, 0x0b})),
	FREE_MEM(0, 3, ARR({0xab, 0xaa, 0x0a, 0x00,
			   0xb0, 0xaa, 0x00, 0x0b})),
	ALLOCATE_MEM(3, 3, ARR({0xab, 0xaa, 0xba, 0x02,
			       0xb0, 0xaa, 0x00, 0x0b})),
	FLUSH_MEM(),
	END(),
};

/* Test first fit align */
static const struct test_command test_first_fit_align[] = {
	SET_ALLOCATOR(gen_pool_first_fit_align, align, 4),
	ALLOCATE_MEM(5, 0, ARR({0xab, 0x02})),
	ALLOCATE_MEM(3, 1, ARR({0xab, 0x02, 0x2b})),
	ALLOCATE_MEM(2, 2, ARR({0xab, 0x02, 0x2b, 0x0b})),
	ALLOCATE_MEM(1, 3, ARR({0xab, 0x02, 0x2b, 0x0b, 0x03})),
	FREE_MEM(0, 0, ARR({0x00, 0x00, 0x2b, 0x0b, 0x03})),
	FREE_MEM(0, 2, ARR({0x00, 0x00, 0x2b, 0x00, 0x03})),
	ALLOCATE_MEM(2, 0, ARR({0x0b, 0x00, 0x2b, 0x00, 0x03})),
	FLUSH_MEM(),
	END(),
};


/* Test fixed alloc */
static const struct test_command test_fixed_data[] = {
	SET_ALLOCATOR(gen_pool_fixed_alloc, offset, 1),
	ALLOCATE_MEM(5, 0, ARR({0xac, 0x0a})),
	SET_ALLOCATOR(gen_pool_fixed_alloc, offset, 8),
	ALLOCATE_MEM(3, 1, ARR({0xac, 0x0a, 0x2b})),
	SET_ALLOCATOR(gen_pool_fixed_alloc, offset, 6),
	ALLOCATE_MEM(2, 2, ARR({0xac, 0xba, 0x2b})),
	SET_ALLOCATOR(gen_pool_fixed_alloc, offset, 30),
	ALLOCATE_MEM(40, 3, ARR({0xac, 0xba, 0x2b, 0x00,
				0x00, 0x00, 0x00, 0xb0,
				0xaa, 0xaa, 0xaa, 0xaa,
				0xaa, 0xaa, 0xaa, 0xaa})),
	FLUSH_MEM(),
	END(),
};


/* Test first fit order align */
static const struct test_command test_first_fit_order_align[] = {
	SET_ALLOCATOR(gen_pool_first_fit_order_align, null, NULL),
	ALLOCATE_MEM(5, 0, ARR({0xab, 0x02})),
	ALLOCATE_MEM(3, 1, ARR({0xab, 0x02, 0x2b})),
	ALLOCATE_MEM(2, 2, ARR({0xab, 0xb2, 0x2b})),
	ALLOCATE_MEM(1, 3, ARR({0xab, 0xbe, 0x2b})),
	ALLOCATE_MEM(1, 4, ARR({0xab, 0xbe, 0xeb})),
	ALLOCATE_MEM(2, 5, ARR({0xab, 0xbe, 0xeb, 0x0b})),
	FLUSH_MEM(),
	END(),
};


/* 007 Test best fit */
static const struct test_command test_best_fit[] = {
	SET_ALLOCATOR(gen_pool_best_fit, null, NULL),
	ALLOCATE_MEM(5, 0, ARR({0xab, 0x02})),
	ALLOCATE_MEM(3, 1, ARR({0xab, 0xae})),
	ALLOCATE_MEM(3, 2, ARR({0xab, 0xae, 0x2b})),
	ALLOCATE_MEM(1, 3, ARR({0xab, 0xae, 0xeb})),
	FREE_MEM(0, 0, ARR({0x00, 0xac, 0xeb})),
	FREE_MEM(0, 2, ARR({0x00, 0xac, 0xc0})),
	ALLOCATE_MEM(2, 0, ARR({0x00, 0xac, 0xcb})),
	FLUSH_MEM(),
	END(),
};


enum test_cases_indexes {
	TEST_CASE_FIRST_FIT,
	TEST_CASE_ULONG_SPAN,
	TEST_CASE_FIRST_FIT_GAPS,
	TEST_CASE_FIRST_FIT_ALIGN,
	TEST_CASE_FIXED_DATA,
	TEST_CASE_FIRST_FIT_ORDER_ALIGN,
	TEST_CASE_BEST_FIT,
	TEST_CASES_NUM,
};

static const struct test_command *test_cases[TEST_CASES_NUM] = {
	[TEST_CASE_FIRST_FIT] = test_first_fit,
	[TEST_CASE_ULONG_SPAN] = test_ulong_span,
	[TEST_CASE_FIRST_FIT_GAPS] = test_first_fit_gaps,
	[TEST_CASE_FIRST_FIT_ALIGN] = test_first_fit_align,
	[TEST_CASE_FIXED_DATA] = test_fixed_data,
	[TEST_CASE_FIRST_FIT_ORDER_ALIGN] = test_first_fit_order_align,
	[TEST_CASE_BEST_FIT] = test_best_fit,
};


void test_genalloc(void)
{
	static struct gen_pool *pool;
	unsigned long locations[ENTRIES];
	char chunk[CHUNK_SIZE];
	int retval;
	unsigned int i;
	const struct test_command *cmd;

	pool = gen_pool_create(ALLOC_ORDER, -1);
	if (unlikely(!pool)) {
		pr_err("genalloc-selftest: no memory for pool.");
		return;
	}

	retval = gen_pool_add_virt(pool, (unsigned long)chunk, 0,
				   CHUNK_SIZE, -1);
	if (unlikely(retval)) {
		pr_err("genalloc-selftest: could not register chunk.");
		goto destroy_pool;
	}

	memset(locations, 0, ENTRIES * sizeof(unsigned long));
	for (i = 0; i < TEST_CASES_NUM; i++)
		for (cmd = test_cases[i]; cmd->command < CMD_END; cmd++)
			callbacks[cmd->command](pool, cmd, locations);
	pr_notice("genalloc-selftest: executed successfully %d tests",
		  TEST_CASES_NUM);

destroy_pool:
	gen_pool_destroy(pool);
}
