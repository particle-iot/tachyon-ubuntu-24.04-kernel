// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_instance.h"
#include "iris_vidc.h"

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

inline bool is_linear_colorformat(u32 colorformat)
{
	return colorformat == V4L2_PIX_FMT_NV12 || colorformat == V4L2_PIX_FMT_NV21;
}

bool is_split_mode_enabled(struct iris_inst *inst)
{
	if (is_linear_colorformat(inst->fmt_dst->fmt.pix_mp.pixelformat))
		return true;

	return false;
}

inline bool is_10bit_colorformat(enum colorformat_type colorformat)
{
	return colorformat == FMT_TP10C;
}

inline bool is_8bit_colorformat(enum colorformat_type colorformat)
{
	return colorformat == FMT_NV12 ||
		colorformat == FMT_NV12C ||
		colorformat == FMT_NV21;
}

u32 v4l2_codec_from_driver(struct iris_inst *inst, enum codec_type codec)
{
	const struct codec_info *codec_info;
	struct iris_core *core;
	u32 v4l2_codec = 0;
	u32 i, size;

	core = inst->core;
	codec_info = core->platform_data->format_data->codec_info;
	size = core->platform_data->format_data->codec_info_size;

	for (i = 0; i < size; i++) {
		if (codec_info[i].codec == codec)
			return codec_info[i].v4l2_codec;
	}

	return v4l2_codec;
}

enum codec_type v4l2_codec_to_driver(struct iris_inst *inst, u32 v4l2_codec)
{
	const struct codec_info *codec_info;
	enum codec_type codec = 0;
	struct iris_core *core;
	u32 i, size;

	core = inst->core;
	codec_info = core->platform_data->format_data->codec_info;
	size = core->platform_data->format_data->codec_info_size;

	for (i = 0; i < size; i++) {
		if (codec_info[i].v4l2_codec == v4l2_codec)
			return codec_info[i].codec;
	}

	return codec;
}

u32 v4l2_colorformat_from_driver(struct iris_inst *inst, enum colorformat_type colorformat)
{
	const struct color_format_info *color_format_info;
	u32 v4l2_colorformat = 0;
	struct iris_core *core;
	u32 i, size;

	core = inst->core;
	color_format_info = core->platform_data->format_data->color_format_info;
	size = core->platform_data->format_data->color_format_info_size;

	for (i = 0; i < size; i++) {
		if (color_format_info[i].color_format == colorformat)
			return color_format_info[i].v4l2_color_format;
	}

	return v4l2_colorformat;
}

enum colorformat_type v4l2_colorformat_to_driver(struct iris_inst *inst, u32 v4l2_colorformat)
{
	const struct color_format_info *color_format_info;
	enum colorformat_type colorformat = 0;
	struct iris_core *core;
	u32 i, size;

	core = inst->core;
	color_format_info = core->platform_data->format_data->color_format_info;
	size = core->platform_data->format_data->color_format_info_size;

	for (i = 0; i < size; i++) {
		if (color_format_info[i].v4l2_color_format == v4l2_colorformat)
			return color_format_info[i].color_format;
	}

	return colorformat;
}

struct vb2_queue *get_vb2q(struct iris_inst *inst, u32 type)
{
	struct vb2_queue *vb2q = NULL;

	switch (type) {
	case INPUT_MPLANE:
		vb2q = inst->vb2q_src;
		break;
	case OUTPUT_MPLANE:
		vb2q = inst->vb2q_dst;
		break;
	default:
		return NULL;
	}

	return vb2q;
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
		mutex_unlock(&inst->lock);
		ret = wait_for_completion_timeout(&inst->completions[SIGNAL_CMD_CLOSE],
						  msecs_to_jiffies(hw_response_timeout_val));
		if (!ret) {
			ret = -ETIMEDOUT;
			process_inst_timeout(inst);
		}
		mutex_lock(&inst->lock);
	}

	return ret;
}

static int check_core_mbps_mbpf(struct iris_inst *inst)
{
	u32 mbpf = 0, mbps = 0, total_mbpf = 0, total_mbps = 0;
	struct iris_core *core;
	struct iris_inst *instance;
	u32 fps;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		fps = inst->cap[QUEUED_RATE].value >> 16;
		mbpf = get_mbpf(inst);
		mbps = mbpf * fps;
		total_mbpf += mbpf;
		total_mbps += mbps;
	}
	mutex_unlock(&core->lock);

	if (total_mbps > core->cap[MAX_MBPS].value ||
	    total_mbpf > core->cap[MAX_MBPF].value)
		return -ENOMEM;

	return 0;
}

static int check_inst_mbpf(struct iris_inst *inst)
{
	u32 mbpf = 0, max_mbpf = 0;

	max_mbpf = inst->cap[MBPF].max;
	mbpf = get_mbpf(inst);
	if (mbpf > max_mbpf)
		return -ENOMEM;

	return 0;
}

static int check_resolution_supported(struct iris_inst *inst)
{
	u32 width = 0, height = 0, min_width, min_height,
		max_width, max_height;

	width = inst->fmt_src->fmt.pix_mp.width;
	height = inst->fmt_src->fmt.pix_mp.height;

	min_width = inst->cap[FRAME_WIDTH].min;
	max_width = inst->cap[FRAME_WIDTH].max;
	min_height = inst->cap[FRAME_HEIGHT].min;
	max_height = inst->cap[FRAME_HEIGHT].max;

	if (!(min_width <= width && width <= max_width) ||
	    !(min_height <= height && height <= max_height))
		return -EINVAL;

	return 0;
}

static int check_max_sessions(struct iris_inst *inst)
{
	struct iris_core *core;
	u32 num_sessions = 0;
	struct iris_inst *i;

	core = inst->core;
	mutex_lock(&core->lock);
	list_for_each_entry(i, &core->instances, list) {
		num_sessions++;
	}
	mutex_unlock(&core->lock);

	if (num_sessions > core->cap[MAX_SESSION_COUNT].value)
		return -ENOMEM;

	return 0;
}

int check_session_supported(struct iris_inst *inst)
{
	int ret;

	ret = check_core_mbps_mbpf(inst);
	if (ret)
		goto exit;

	ret = check_inst_mbpf(inst);
	if (ret)
		goto exit;

	ret = check_resolution_supported(inst);
	if (ret)
		goto exit;

	ret = check_max_sessions(inst);
	if (ret)
		goto exit;

	return ret;
exit:
	dev_err(inst->core->dev, "current session not supported(%d)\n", ret);

	return ret;
}

int signal_session_msg_receipt(struct iris_inst *inst,
			       enum signal_session_response cmd)
{
	if (cmd < MAX_SIGNAL)
		complete(&inst->completions[cmd]);

	return 0;
}

struct iris_inst *to_instance(struct iris_core *core, u32 session_id)
{
	struct iris_inst *inst = NULL;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			mutex_unlock(&core->lock);
			return inst;
		}
	}
	mutex_unlock(&core->lock);

	return NULL;
}

static int kill_session(struct iris_inst *inst)
{
	if (!inst->session_id)
		return 0;

	close_session(inst);
	iris_inst_change_state(inst, IRIS_INST_ERROR);

	return 0;
}

int session_streamoff(struct iris_inst *inst, u32 plane)
{
	enum signal_session_response signal_type;
	u32 hw_response_timeout_val;
	struct iris_core *core;
	int ret;

	ret = iris_hfi_stop(inst, plane);
	if (ret)
		goto error;

	core = inst->core;
	hw_response_timeout_val = core->cap[HW_RESPONSE_TIMEOUT].value;
	mutex_unlock(&inst->lock);
	ret = wait_for_completion_timeout(&inst->completions[signal_type],
					  msecs_to_jiffies(hw_response_timeout_val));
	if (!ret) {
		ret = -ETIMEDOUT;
		process_inst_timeout(inst);
	} else {
		ret = 0;
	}
	mutex_lock(&inst->lock);

	if (ret)
		goto error;

	ret = iris_inst_state_change_streamoff(inst, plane);
	if (ret)
		goto error;

	return 0;

error:
	kill_session(inst);

	return ret;
}
