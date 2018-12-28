/* SPDX-License-Identifier: GPL-2.0 */
/*
 * memset_user.S - memset for userspace on arm64
 *
 * (C) Copyright 2018 Huawey Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 * Based on arch/arm64/lib/clear_user.S
 */

#include <linux/linkage.h>

#include <asm/asm-uaccess.h>

	.text

/* Prototype: int __arch_memset_user(void *addr, int c, size_t n)
 * Purpose  : set n bytes of user memory at "addr" to the value "c"
 * Params   : x0 - addr, user memory address to set
 *          : x1 - c, byte value
 *          : x2 - n, number of bytes to set
 * Returns  : number of bytes NOT set
 *
 * Alignment fixed up by hardware.
 */
ENTRY(__arch_memset_user)
	uaccess_enable_not_uao x3, x4, x5
	// replicate the byte to the whole register
	and	x1, x1, 0xff
	lsl	x3, x1, 8
	orr	x1, x3, x1
	lsl	x3, x1, 16
	orr 	x1, x3, x1
	lsl	x3, x1, 32
	orr	x1, x3, x1
	mov	x3, x2			// save the size for fixup return
	subs	x2, x2, #8
	b.mi	2f
1:
uao_user_alternative 9f, str, sttr, x1, x0, 8
	subs	x2, x2, #8
	b.pl	1b
2:	adds	x2, x2, #4
	b.mi	3f
uao_user_alternative 9f, str, sttr, x1, x0, 4
	sub	x2, x2, #4
3:	adds	x2, x2, #2
	b.mi	4f
uao_user_alternative 9f, strh, sttrh, w1, x0, 2
	sub	x2, x2, #2
4:	adds	x2, x2, #1
	b.mi	5f
uao_user_alternative 9f, strb, sttrb, w1, x0, 0
5:	mov	x0, #0
	uaccess_disable_not_uao x3, x4
	ret
ENDPROC(__arch_memset_user)

	.section .fixup,"ax"
	.align	2
9:	mov	x0, x3			// return the original size
	ret
	.previous
