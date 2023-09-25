// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_instance.h"
#include "vpu_iris3_buffer.h"

static u32 dec_bin_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (inst->codec == H264)
		size = hfi_buffer_bin_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_bin_h265d(width, height, num_vpp_pipes);
	else if (inst->codec == VP9)
		size = hfi_buffer_bin_vp9d(width, height,
					   num_vpp_pipes);
	return size;
}

static u32 dec_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_comv;
	struct v4l2_format *f;
	u32 size = 0;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	num_comv = inst->buffers.output.min_count;

	if (inst->codec == H264)
		size = hfi_buffer_comv_h264d(width, height, num_comv);
	else if (inst->codec == HEVC)
		size = hfi_buffer_comv_h265d(width, height, num_comv);

	inst->cap[NUM_COMV].value = num_comv;

	return size;
}

static u32 dec_non_comv_size_iris3(struct iris_inst *inst)
{
	u32 width, height, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;

	core = inst->core;

	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (inst->codec == H264)
		size = hfi_buffer_non_comv_h264d(width, height, num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_non_comv_h265d(width, height, num_vpp_pipes);

	return size;
}

static u32 dec_line_size_iris3(struct iris_inst *inst)
{
	u32 width, height, out_min_count, num_vpp_pipes;
	struct iris_core *core;
	struct v4l2_format *f;
	u32 size = 0;
	bool is_opb;

	core = inst->core;
	num_vpp_pipes = core->cap[NUM_VPP_PIPE].value;

	is_opb = true;

	f = inst->fmt_src;
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;
	out_min_count = inst->buffers.output.min_count;
	if (inst->codec == H264)
		size = hfi_buffer_line_h264d(width, height, is_opb,
					     num_vpp_pipes);
	else if (inst->codec == HEVC)
		size = hfi_buffer_line_h265d(width, height, is_opb,
					     num_vpp_pipes);
	else if (inst->codec == VP9)
		size = hfi_buffer_line_vp9d(width, height, out_min_count,
					    is_opb, num_vpp_pipes);
	return size;
}

static u32 dec_persist_size_iris3(struct iris_inst *inst)
{
	u32 size = 0;

	if (inst->codec == H264)
		size = hfi_buffer_persist_h264d(0);
	else if (inst->codec == HEVC)
		size = hfi_buffer_persist_h265d(0);
	else if (inst->codec == VP9)
		size = hfi_buffer_persist_vp9d();

	return size;
}

static u32 dec_dpb_size_iris3(struct iris_inst *inst)
{
	struct v4l2_format *f;
	u32 width, height;
	u32 color_fmt;
	u32 size = 0;

	f = inst->fmt_dst;
	color_fmt = f->fmt.pix_mp.pixelformat;
	if (!is_linear_colorformat(color_fmt))
		return size;

	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;

	if (color_fmt == V4L2_PIX_FMT_NV12 ||
	    color_fmt == V4L2_PIX_FMT_QC08C) {
		size =
		hfi_nv12_ubwc_il_calc_buf_size_v2(width, height,
						  ALIGN(width, 128),
						  ALIGN(height, 32),
						  ALIGN(width, 128),
						  ALIGN((height + 1) >> 1, 32),
						  ALIGN(DIV_ROUND_UP(width, 32), 64),
						  ALIGN(DIV_ROUND_UP(height, 8), 16),
						  ALIGN(DIV_ROUND_UP((width + 1) >> 1, 16), 64),
						  ALIGN(DIV_ROUND_UP((height + 1) >> 1, 8), 16));
	} else if (color_fmt == V4L2_PIX_FMT_QC10C) {
		size =
		hfi_yuv420_tp10_ubwc_calc_buf_size(ALIGN(ALIGN(width, 192) * 4 / 3, 256),
						   ALIGN(height, 16),
						   ALIGN(ALIGN(width, 192) * 4 / 3, 256),
						   ALIGN((height + 1) >> 1, 16),
						   ALIGN(DIV_ROUND_UP(width, 48), 64),
						   ALIGN(DIV_ROUND_UP(height, 4), 16),
						   ALIGN(DIV_ROUND_UP((width + 1) >> 1, 24), 64),
						   ALIGN(DIV_ROUND_UP((height + 1) >> 1, 4), 16));
	}

	return size;
}

struct iris_buf_type_handle {
	enum iris_buffer_type type;
	u32 (*handle)(struct iris_inst *inst);
};

int iris_int_buf_size_iris3(struct iris_inst *inst,
			    enum iris_buffer_type buffer_type)
{
	const struct iris_buf_type_handle *buf_type_handle_arr = NULL;
	u32 size = 0, buf_type_handle_size = 0;
	int i;

	static const struct iris_buf_type_handle dec_internal_buf_type_handle[] = {
		{BUF_BIN,             dec_bin_size_iris3          },
		{BUF_COMV,            dec_comv_size_iris3         },
		{BUF_NON_COMV,        dec_non_comv_size_iris3     },
		{BUF_LINE,            dec_line_size_iris3         },
		{BUF_PERSIST,         dec_persist_size_iris3      },
		{BUF_DPB,             dec_dpb_size_iris3          },
	};

	buf_type_handle_size = ARRAY_SIZE(dec_internal_buf_type_handle);
	buf_type_handle_arr = dec_internal_buf_type_handle;

	if (!buf_type_handle_arr || !buf_type_handle_size)
		return size;

	for (i = 0; i < buf_type_handle_size; i++) {
		if (buf_type_handle_arr[i].type == buffer_type) {
			size = buf_type_handle_arr[i].handle(inst);
			break;
		}
	}

	return size;
}
