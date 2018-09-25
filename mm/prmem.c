// SPDX-License-Identifier: GPL-2.0
/*
 * prmem.c: Memory Protection Library
 *
 * (C) Copyright 2017-2018 Huawei Technologies Co. Ltd.
 * Author: Igor Stoppa <igor.stoppa@huawei.com>
 */

const char WR_ERR_RANGE_MSG[] = "Write rare on invalid memory range.";
const char WR_ERR_PAGE_MSG[] = "Failed to remap write rare page.";
