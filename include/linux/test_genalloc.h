/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_genalloc.h
 *
 * (C) Copyright 2017 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */


#ifndef __LINUX_TEST_GENALLOC_H
#define __LINUX_TEST_GENALLOC_H


#ifdef CONFIG_TEST_GENERIC_ALLOCATOR

#include <linux/genalloc.h>

void test_genalloc(void);

#else

static inline void test_genalloc(void){};

#endif

#endif
