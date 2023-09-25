// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_instance.h"

int check_core_lock(struct iris_core *core)
{
	bool fatal = !mutex_is_locked(&core->lock);

	WARN_ON(fatal);

	return fatal ? -EINVAL : 0;
}

bool res_is_less_than(u32 width, u32 height,
		      u32 ref_width, u32 ref_height)
{
	u32 num_mbs = NUM_MBS_PER_FRAME(height, width);
	u32 max_side = max(ref_width, ref_height);

	if (num_mbs < NUM_MBS_PER_FRAME(ref_height, ref_width) &&
	    width < max_side &&
	    height < max_side)
		return true;

	return false;
}

u32 get_port_info(struct iris_inst *inst,
		  enum plat_inst_cap_type cap_id)
{
	if (inst->cap[cap_id].flags & CAP_FLAG_INPUT_PORT)
		return HFI_PORT_BITSTREAM;
	else if (inst->cap[cap_id].flags & CAP_FLAG_OUTPUT_PORT)
		return HFI_PORT_RAW;

	return HFI_PORT_NONE;
}

enum iris_buffer_type v4l2_type_to_driver(u32 type)
{
	switch (type) {
	case INPUT_MPLANE:
		return BUF_INPUT;
	case OUTPUT_MPLANE:
		return BUF_OUTPUT;
	default:
		return 0;
	}
}

int get_mbpf(struct iris_inst *inst)
{
	int height = 0, width = 0;
	struct v4l2_format *inp_f;

	inp_f = inst->fmt_src;
	width = max(inp_f->fmt.pix_mp.width, inst->crop.width);
	height = max(inp_f->fmt.pix_mp.height, inst->crop.height);

	return NUM_MBS_PER_FRAME(height, width);
}

bool is_linear_colorformat(u32 colorformat)
{
	return colorformat == V4L2_PIX_FMT_NV12 || colorformat == V4L2_PIX_FMT_NV21;
}

bool is_split_mode_enabled(struct iris_inst *inst)
{
	if (is_linear_colorformat(inst->fmt_dst->fmt.pix_mp.pixelformat))
		return true;

	return false;
}

static int process_inst_timeout(struct iris_inst *inst)
{
	struct iris_inst *instance;
	struct iris_core *core;
	bool found = false;
	int ret = 0;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		if (instance == inst) {
			found = true;
			break;
		}
	}
	if (!found) {
		ret = -EINVAL;
		goto unlock;
	}

	iris_change_core_state(core, IRIS_CORE_ERROR);

	iris_core_deinit_locked(core);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int close_session(struct iris_inst *inst)
{
	u32 hw_response_timeout_val;
	bool wait_for_response;
	struct iris_core *core;
	int ret;

	core = inst->core;
	hw_response_timeout_val = core->cap[HW_RESPONSE_TIMEOUT].value;
	wait_for_response = true;
	ret = iris_hfi_session_close(inst);
	if (ret)
		wait_for_response = false;

	kfree(inst->packet);
	inst->packet = NULL;

	if (wait_for_response) {
		ret = wait_for_completion_timeout(&inst->completions[SIGNAL_CMD_CLOSE],
						  msecs_to_jiffies(hw_response_timeout_val));
		if (!ret) {
			ret = -ETIMEDOUT;
			process_inst_timeout(inst);
		}
	}

	return ret;
}
