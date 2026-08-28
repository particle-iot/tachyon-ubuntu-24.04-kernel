// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-mem2mem.h>

#include "iris_instance.h"
#include "iris_utils.h"

/*
 * Parity and bounds normalization for the encoder's visible frame geometry,
 * shared by TRY_FMT, S_FMT and S_SELECTION so that the three cannot disagree
 * on it.
 *
 * Odd sizes are rounded down because the H.264 and HEVC cropping syntax counts
 * in chroma samples: with 4:2:0 subsampling an odd visible width or height
 * cannot be expressed in the bitstream at all.
 *
 * The bounds are the ones VIDIOC_ENUM_FRAMESIZES reports, so the two cannot
 * drift apart. This covers geometry only - it says nothing about whether the
 * firmware will accept a given combination.
 */
void iris_normalize_frame_size(struct iris_inst *inst, u32 *width, u32 *height)
{
	struct platform_inst_caps *caps = inst->core->iris_platform_data->inst_caps;

	*width = clamp(*width & ~1u, caps->min_frame_width, caps->max_frame_width);
	*height = clamp(*height & ~1u, caps->min_frame_height, caps->max_frame_height);
}

bool iris_res_is_less_than(u32 width, u32 height,
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

int iris_get_mbpf(struct iris_inst *inst)
{
	struct v4l2_format *inp_f = inst->fmt_src;
	u32 height = max(inp_f->fmt.pix_mp.height, inst->crop.height);
	u32 width = max(inp_f->fmt.pix_mp.width, inst->crop.width);

	return NUM_MBS_PER_FRAME(height, width);
}

bool iris_split_mode_enabled(struct iris_inst *inst)
{
	return inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_NV12 ||
		inst->fmt_dst->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC08C;
}

void iris_helper_buffers_done(struct iris_inst *inst, unsigned int type,
			      enum vb2_buffer_state state)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->m2m_ctx;
	struct vb2_v4l2_buffer *buf;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		while ((buf = v4l2_m2m_src_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	} else if (V4L2_TYPE_IS_CAPTURE(type)) {
		while ((buf = v4l2_m2m_dst_buf_remove(m2m_ctx)))
			v4l2_m2m_buf_done(buf, state);
	}
}

int iris_wait_for_session_response(struct iris_inst *inst, bool is_flush)
{
	struct iris_core *core = inst->core;
	u32 hw_response_timeout_val;
	struct completion *done;
	int ret;

	hw_response_timeout_val = core->iris_platform_data->hw_response_timeout;
	done = is_flush ? &inst->flush_completion : &inst->completion;

	mutex_unlock(&inst->lock);
	ret = wait_for_completion_timeout(done, msecs_to_jiffies(hw_response_timeout_val));
	mutex_lock(&inst->lock);
	if (!ret) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Returns the instance with a reference held; the caller must drop it with
 * iris_put_instance().
 *
 * Handing out a bare pointer here is not safe. The lookup runs from the
 * interrupt path, which then takes inst->lock - and between dropping
 * core->lock and acquiring inst->lock, close() can remove the session,
 * destroy that very mutex and free the instance. The firmware's ordering is
 * not a lifetime guarantee either: a failed close command, a close response
 * that times out, a late or out-of-order packet, or error recovery all leave
 * a response in flight for a session that is going away.
 *
 * Taking the reference under core->lock closes that window. The instance is
 * either still on the list, in which case it cannot be freed until the
 * caller is done with it, or already removed, in which case the lookup
 * simply misses it.
 */
struct iris_inst *iris_get_instance(struct iris_core *core, u32 session_id)
{
	struct iris_inst *inst;

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list) {
		if (inst->session_id == session_id) {
			kref_get(&inst->kref);
			mutex_unlock(&core->lock);
			return inst;
		}
	}

	mutex_unlock(&core->lock);
	return NULL;
}

int iris_check_core_mbpf(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *instance;
	u32 total_mbpf = 0;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list)
		total_mbpf += iris_get_mbpf(instance);
	mutex_unlock(&core->lock);

	if (total_mbpf > core->iris_platform_data->max_core_mbpf)
		return -ENOMEM;

	return 0;
}

int iris_check_core_mbps(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
	struct iris_inst *instance;
	u32 total_mbps = 0, fps = 0;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		fps = max(instance->frame_rate, instance->operating_rate);
		total_mbps += iris_get_mbpf(instance) * fps;
	}
	mutex_unlock(&core->lock);

	if (total_mbps > core->iris_platform_data->max_core_mbps)
		return -ENOMEM;

	return 0;
}
