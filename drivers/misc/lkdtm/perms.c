// SPDX-License-Identifier: GPL-2.0
/*
 * This is for all the tests related to validating kernel memory
 * permissions: non-executable regions, non-writable regions, and
 * even non-readable regions.
 */
#include "lkdtm.h"
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/prmemextra.h>
#include <asm/cacheflush.h>

/* Whether or not to fill the target memory area with do_nothing(). */
#define CODE_WRITE	true
#define CODE_AS_IS	false

/* How many bytes to copy to be sure we've copied enough of do_nothing(). */
#define EXEC_SIZE 64

/* This is non-const, so it will end up in the .data section. */
static u8 data_area[EXEC_SIZE];

/* This is cost, so it will end up in the .rodata section. */
static const unsigned long rodata = 0xAA55AA55;

/* This is marked __ro_after_init, so it should ultimately be .rodata. */
static unsigned long ro_after_init __ro_after_init = 0x55AA5500;

/* This is marked __wr_after_init, so it should be in .rodata. */
static
unsigned long wr_after_init __wr_after_init = 0x55AA5500;

/*
 * This just returns to the caller. It is designed to be copied into
 * non-executable memory regions.
 */
static void do_nothing(void)
{
	return;
}

/* Must immediately follow do_nothing for size calculuations to work out. */
static void do_overwritten(void)
{
	pr_info("do_overwritten wasn't overwritten!\n");
	return;
}

static noinline void execute_location(void *dst, bool write)
{
	void (*func)(void) = dst;

	pr_info("attempting ok execution at %p\n", do_nothing);
	do_nothing();

	if (write == CODE_WRITE) {
		memcpy(dst, do_nothing, EXEC_SIZE);
		flush_icache_range((unsigned long)dst,
				   (unsigned long)dst + EXEC_SIZE);
	}
	pr_info("attempting bad execution at %p\n", func);
	func();
}

static void execute_user_location(void *dst)
{
	int copied;

	/* Intentionally crossing kernel/user memory boundary. */
	void (*func)(void) = dst;

	pr_info("attempting ok execution at %p\n", do_nothing);
	do_nothing();

	copied = access_process_vm(current, (unsigned long)dst, do_nothing,
				   EXEC_SIZE, FOLL_WRITE);
	if (copied < EXEC_SIZE)
		return;
	pr_info("attempting bad execution at %p\n", func);
	func();
}

void lkdtm_WRITE_RO(void)
{
	/* Explicitly cast away "const" for the test. */
	unsigned long *ptr = (unsigned long *)&rodata;

	pr_info("attempting bad rodata write at %p\n", ptr);
	*ptr ^= 0xabcd1234;
}

void lkdtm_WRITE_RO_AFTER_INIT(void)
{
	unsigned long *ptr = &ro_after_init;

	/*
	 * Verify we were written to during init. Since an Oops
	 * is considered a "success", a failure is to just skip the
	 * real test.
	 */
	if ((*ptr & 0xAA) != 0xAA) {
		pr_info("%p was NOT written during init!?\n", ptr);
		return;
	}

	pr_info("attempting bad ro_after_init write at %p\n", ptr);
	*ptr ^= 0xabcd1234;
}

void lkdtm_WRITE_WR_AFTER_INIT(void)
{
	unsigned long *ptr = &wr_after_init;

	/*
	 * Verify we were written to during init. Since an Oops
	 * is considered a "success", a failure is to just skip the
	 * real test.
	 */
	if ((*ptr & 0xAA) != 0xAA) {
		pr_info("%p was NOT written during init!?\n", ptr);
		return;
	}

	pr_info("attempting bad wr_after_init write at %p\n", ptr);
	*ptr ^= 0xabcd1234;
}

#define INIT_VAL 0x5A
#define END_VAL 0xA5

/* Verify that write rare will not work against read-only memory. */
static int ro_after_init_data __ro_after_init = INIT_VAL;
void lkdtm_WRITE_WR_AFTER_INIT_ON_RO_AFTER_INIT(void)
{
	pr_info("attempting illegal write rare to __ro_after_init");
	if (wr_int(&ro_after_init_data, END_VAL) ||
	     ro_after_init_data == END_VAL)
		pr_info("Unexpected successful write to __ro_after_init");
}

/*
 * "volatile" to force the compiler to not optimize away the reading back.
 * Is there a better way to do it, than using volatile?
 */
static volatile const int const_data = INIT_VAL;
void lkdtm_WRITE_WR_AFTER_INIT_ON_CONST(void)
{
	pr_info("attempting illegal write rare to const data");
	if (wr_int((int *)&const_data, END_VAL) || const_data == END_VAL)
		pr_info("Unexpected successful write to const memory");
}

#ifdef CONFIG_PRMEM

#define MSG_NO_POOL "Cannot allocate memory for the pool."
#define MSG_NO_PMEM "Cannot allocate memory from the pool."

void lkdtm_WRITE_RO_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *i;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	i = pmalloc(pool, sizeof(int));
	if (!i) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*i = INT_MAX;
	pmalloc_protect_pool(pool);
	pr_info("attempting bad pmalloc write at %p\n", i);
	*i = 0; /* Note: this will crash and leak the pool memory. */
}

void lkdtm_WRITE_AUTO_RO_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *i;

	pool = pmalloc_create_pool(PMALLOC_AUTO_RO);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	i = pmalloc(pool, sizeof(int));
	if (!i) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*i = INT_MAX;
	pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	pr_info("attempting bad pmalloc write at %p\n", i);
	*i = 0; /* Note: this will crash and leak the pool memory. */
}

void lkdtm_WRITE_WR_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *i;

	pool = pmalloc_create_pool(PMALLOC_WR);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	i = pmalloc(pool, sizeof(int));
	if (!i) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*i = INT_MAX;
	pmalloc_protect_pool(pool);
	pr_info("attempting bad pmalloc write at %p\n", i);
	*i = 0; /* Note: this will crash and leak the pool memory. */
}

void lkdtm_WRITE_AUTO_WR_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *i;

	pool = pmalloc_create_pool(PMALLOC_AUTO_WR);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	i = pmalloc(pool, sizeof(int));
	if (!i) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*i = INT_MAX;
	pmalloc(pool, PMALLOC_DEFAULT_REFILL_SIZE);
	pr_info("attempting bad pmalloc write at %p\n", i);
	*i = 0; /* Note: this will crash and leak the pool memory. */
}

void lkdtm_WRITE_START_WR_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *i;

	pool = pmalloc_create_pool(PMALLOC_START_WR);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	i = pmalloc(pool, sizeof(int));
	if (!i) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*i = INT_MAX;
	pr_info("attempting bad pmalloc write at %p\n", i);
	*i = 0; /* Note: this will crash and leak the pool memory. */
}

void lkdtm_WRITE_WR_PMALLOC_ON_RO_PMALLOC(void)
{
	struct pmalloc_pool *pool;
	int *var_ptr;

	pool = pmalloc_create_pool(PMALLOC_RO);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	var_ptr = pmalloc(pool, sizeof(int));
	if (!var_ptr) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*var_ptr = INIT_VAL;
	pmalloc_protect_pool(pool);
	pr_info("attempting illegal write rare to R/O pool");
	if (wr_int(var_ptr, END_VAL))
		pr_info("Unexpected successful write to R/O pool");
	pmalloc_destroy_pool(pool);
}

void lkdtm_WRITE_WR_PMALLOC_ON_CONST(void)
{
	struct pmalloc_pool *pool;
	int *dummy;
	bool write_result;

	/*
	 * The pool operations are only meant to simulate an attacker
	 * using a random pool as parameter for the attack against the
	 * const.
	 */
	pool = pmalloc_create_pool(PMALLOC_WR);
	if (!pool) {
		pr_info(MSG_NO_POOL);
		return;
	}
	dummy = pmalloc(pool, sizeof(*dummy));
	if (!dummy) {
		pr_info(MSG_NO_PMEM);
		pmalloc_destroy_pool(pool);
		return;
	}
	*dummy = 1;
	pmalloc_protect_pool(pool);
	pr_info("attempting illegal write rare to const data");
	write_result = wr_int((int *)&const_data, END_VAL);
	pmalloc_destroy_pool(pool);
	if (write_result || const_data != INIT_VAL)
		pr_info("Unexpected successful write to const memory");
}

void lkdtm_WRITE_WR_PMALLOC_ON_RO_AFT_INIT(void)
{
	struct pmalloc_pool *pool;
	int *dummy;
	bool write_result;

	/*
	 * The pool operations are only meant to simulate an attacker
	 * using a random pool as parameter for the attack against the
	 * const.
	 */
	pool = pmalloc_create_pool(PMALLOC_WR);
	if (WARN(!pool, MSG_NO_POOL))
		return;
	dummy = pmalloc(pool, sizeof(*dummy));
	if (WARN(!dummy, MSG_NO_PMEM)) {
		pmalloc_destroy_pool(pool);
		return;
	}
	*dummy = 1;
	pmalloc_protect_pool(pool);
	pr_info("attempting illegal write rare to ro_after_init");
	write_result = wr_int(&ro_after_init_data, END_VAL);
	pmalloc_destroy_pool(pool);
	WARN(write_result || ro_after_init_data != INIT_VAL,
	     "Unexpected successful write to ro_after_init memory");
}
#endif

void lkdtm_WRITE_KERN(void)
{
	size_t size;
	unsigned char *ptr;

	size = (unsigned long)do_overwritten - (unsigned long)do_nothing;
	ptr = (unsigned char *)do_overwritten;

	pr_info("attempting bad %zu byte write at %p\n", size, ptr);
	memcpy(ptr, (unsigned char *)do_nothing, size);
	flush_icache_range((unsigned long)ptr, (unsigned long)(ptr + size));

	do_overwritten();
}

void lkdtm_EXEC_DATA(void)
{
	execute_location(data_area, CODE_WRITE);
}

void lkdtm_EXEC_STACK(void)
{
	u8 stack_area[EXEC_SIZE];
	execute_location(stack_area, CODE_WRITE);
}

void lkdtm_EXEC_KMALLOC(void)
{
	u32 *kmalloc_area = kmalloc(EXEC_SIZE, GFP_KERNEL);
	execute_location(kmalloc_area, CODE_WRITE);
	kfree(kmalloc_area);
}

void lkdtm_EXEC_VMALLOC(void)
{
	u32 *vmalloc_area = vmalloc(EXEC_SIZE);
	execute_location(vmalloc_area, CODE_WRITE);
	vfree(vmalloc_area);
}

void lkdtm_EXEC_RODATA(void)
{
	execute_location(lkdtm_rodata_do_nothing, CODE_AS_IS);
}

void lkdtm_EXEC_USERSPACE(void)
{
	unsigned long user_addr;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}
	execute_user_location((void *)user_addr);
	vm_munmap(user_addr, PAGE_SIZE);
}

void lkdtm_ACCESS_USERSPACE(void)
{
	unsigned long user_addr, tmp = 0;
	unsigned long *ptr;

	user_addr = vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (user_addr >= TASK_SIZE) {
		pr_warn("Failed to allocate user memory\n");
		return;
	}

	if (copy_to_user((void __user *)user_addr, &tmp, sizeof(tmp))) {
		pr_warn("copy_to_user failed\n");
		vm_munmap(user_addr, PAGE_SIZE);
		return;
	}

	ptr = (unsigned long *)user_addr;

	pr_info("attempting bad read at %p\n", ptr);
	tmp = *ptr;
	tmp += 0xc0dec0de;

	pr_info("attempting bad write at %p\n", ptr);
	*ptr = tmp;

	vm_munmap(user_addr, PAGE_SIZE);
}

void __init lkdtm_perms_init(void)
{
	/* Make sure we can write to __ro_after_init values during __init */
	ro_after_init |= 0xAA;

	/* Make sure we can write to __wr_after_init during __init */
	wr_after_init |= 0xAA;
}
