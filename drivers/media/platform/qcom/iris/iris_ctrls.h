/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CTRLS_H_
#define _IRIS_CTRLS_H_

#include "iris_instance.h"

struct cap_entry {
	struct list_head list;
	enum plat_inst_cap_type cap_id;
};

int set_u32_enum(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_stage(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_pipe(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int set_u32(struct iris_inst *inst, enum plat_inst_cap_type cap_id);
int adjust_output_order(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int adjust_profile(struct iris_inst *inst, struct v4l2_ctrl *ctrl);
int vidc_ctrl_handler_deinit(struct iris_inst *inst);
int prepare_dependency_list(struct iris_inst *inst);
int iris_init_instance_caps(struct iris_core *core);
int iris_init_core_caps(struct iris_core *core);
int get_inst_capability(struct iris_inst *inst);
int adjust_v4l2_properties(struct iris_inst *inst);
int ctrls_init(struct iris_inst *inst);

#endif
