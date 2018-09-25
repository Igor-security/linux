// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library - x86_64 backend
 *
 * (C) Copyright 2018-2019 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

#include <linux/mm.h>
#include <linux/mmu_context.h>

unsigned long __init __init_wr_base(void)
{
	/*
	 * Place 64TB of kernel address space within 128TB of user address
	 * space, at a random page aligned offset.
	 */
	return (((unsigned long)kaslr_get_random_long("WR Poke")) &
		PAGE_MASK) % (64 * _BITUL(40));
}
