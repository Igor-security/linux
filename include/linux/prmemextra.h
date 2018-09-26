/* SPDX-License-Identifier: GPL-2.0 */
/*
 * prmemextra.h: Shorthands for write rare of basic data types
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 *
 */

#ifndef _LINUX_PRMEMEXTRA_H
#define _LINUX_PRMEMEXTRA_H

#include <linux/prmem.h>

/**
 * wr_char - alters a char in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_char(const char *dst, const char val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_short - alters a short in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_short(const short *dst, const short val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_ushort - alters an unsigned short in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ushort(const unsigned short *dst, const unsigned short val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_int - alters an int in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_int(const int *dst, const int val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_uint - alters an unsigned int in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_uint(const unsigned int *dst, const unsigned int val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_long - alters a long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_long(const long *dst, const long val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_ulong - alters an unsigned long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ulong(const unsigned long *dst, const unsigned long val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_longlong - alters a long long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_longlong(const long long *dst, const long long val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

/**
 * wr_ulonglong - alters an unsigned long long in write rare memory
 * @dst: target for write
 * @val: new value
 *
 * Returns true on success, false otherwise.
 */
static __always_inline
bool wr_ulonglong(const unsigned long long *dst,
			  const unsigned long long val)
{
	return wr_memcpy(dst, &val, sizeof(val));
}

#endif
