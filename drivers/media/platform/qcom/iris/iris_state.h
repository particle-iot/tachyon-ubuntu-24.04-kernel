/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_STATE_H_
#define _IRIS_STATE_H_

struct iris_core;
struct iris_inst;

enum iris_core_state {
	IRIS_CORE_DEINIT,
	IRIS_CORE_INIT_WAIT,
	IRIS_CORE_INIT,
	IRIS_CORE_ERROR,
};

enum iris_inst_state {
	IRIS_INST_OPEN,
	IRIS_INST_INPUT_STREAMING,
	IRIS_INST_OUTPUT_STREAMING,
	IRIS_INST_STREAMING,
	IRIS_INST_CLOSE,
	IRIS_INST_ERROR,
};

enum state_change {
	STATE_CHANGE_ALLOW,
	STATE_CHANGE_DISALLOW,
	STATE_CHANGE_IGNORE,
};

#define IS_SESSION_ERROR(inst) \
((inst)->state == IRIS_INST_ERROR)

bool core_in_valid_state(struct iris_core *core);
int iris_change_core_state(struct iris_core *core,
			   enum iris_core_state request_state);

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state);

bool allow_s_fmt(struct iris_inst *inst, u32 type);
bool allow_reqbufs(struct iris_inst *inst, u32 type);
bool allow_streamon(struct iris_inst *inst, u32 type);
bool allow_streamoff(struct iris_inst *inst, u32 type);
bool allow_s_ctrl(struct iris_inst *inst, u32 cap_id);

int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane);
int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane);

#endif
