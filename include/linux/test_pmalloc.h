/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_pmalloc.h
 *
 * (C) Copyright 2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */


#ifndef __LINUX_TEST_PMALLOC_H
#define __LINUX_TEST_PMALLOC_H


#ifdef CONFIG_TEST_PROTECTABLE_MEMORY

void test_pmalloc(void);

#else

static inline void test_pmalloc(void){};

#endif

#endif
