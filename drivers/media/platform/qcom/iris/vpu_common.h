/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _VPU_COMMON_H_
#define _VPU_COMMON_H_

#include <linux/types.h>

struct iris_core;

#define call_vpu_op(d, op, ...)			\
	(((d) && (d)->vpu_ops && (d)->vpu_ops->op) ? \
	((d)->vpu_ops->op(__VA_ARGS__)) : 0)

struct compat_handle {
	const char *compat;
	int (*init)(struct iris_core *core);
};

struct vpu_ops {
	int (*boot_firmware)(struct iris_core *core);
	int (*raise_interrupt)(struct iris_core *core);
};

int init_vpu(struct iris_core *core);

int write_register(struct iris_core *core, u32 reg, u32 value);
int read_register(struct iris_core *core, u32 reg, u32 *value);

#endif
