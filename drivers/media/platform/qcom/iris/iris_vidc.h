/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VIDC_H__
#define __IRIS_VIDC_H__

struct iris_core;
struct iris_inst;

void iris_init_ops(struct iris_core *core);
int iris_open(struct file *filp);
int iris_add_session(struct iris_inst *inst);
void iris_remove_session(struct iris_inst *inst);
int iris_close(struct file *filp);

#endif
