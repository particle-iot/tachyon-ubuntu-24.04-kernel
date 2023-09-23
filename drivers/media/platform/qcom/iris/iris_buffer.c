// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_helpers.h"
#include "iris_instance.h"
#include "memory.h"

static unsigned int video_buffer_size(unsigned int colorformat,
				      unsigned int pix_width,
				      unsigned int pix_height)
{
	unsigned int size = 0;
	unsigned int y_plane, uv_plane, y_stride,
		uv_stride, y_sclines, uv_sclines;
	unsigned int y_ubwc_plane = 0, uv_ubwc_plane = 0;
	unsigned int y_meta_stride = 0, y_meta_scanlines = 0;
	unsigned int uv_meta_stride = 0, uv_meta_scanlines = 0;
	unsigned int y_meta_plane = 0, uv_meta_plane = 0;

	if (!pix_width || !pix_height)
		goto invalid_input;

	switch (colorformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		y_stride = ALIGN(pix_width, 128);
		uv_stride = ALIGN(pix_width, 128);
		y_sclines = ALIGN(pix_height, 32);
		uv_sclines = ALIGN((pix_height + 1) >> 1, 16);
		y_plane = y_stride * y_sclines;
		uv_plane = uv_stride * uv_sclines;
		size = y_plane + uv_plane;
		break;
	case V4L2_PIX_FMT_QC08C:
		y_stride = ALIGN(pix_width, 128);
		uv_stride = ALIGN(pix_width, 128);
		y_sclines = ALIGN(pix_height, 32);
		uv_sclines = ALIGN((pix_height + 1) >> 1, 32);
		y_meta_stride = ALIGN(DIV_ROUND_UP(pix_width, 32), 64);
		uv_meta_stride = ALIGN(DIV_ROUND_UP((pix_width + 1) >> 1, 16), 64);
		y_ubwc_plane =
			ALIGN(y_stride * y_sclines, 4096);
		uv_ubwc_plane =
			ALIGN(uv_stride * uv_sclines, 4096);
		y_meta_scanlines =
			ALIGN(DIV_ROUND_UP(pix_height, 8), 16);
		y_meta_plane =
			ALIGN(y_meta_stride * y_meta_scanlines, 4096);
		uv_meta_scanlines =
			ALIGN(DIV_ROUND_UP((pix_height + 1) >> 1, 8), 16);
		uv_meta_plane =
			ALIGN(uv_meta_stride * uv_meta_scanlines, 4096);
		size = (y_ubwc_plane + uv_ubwc_plane + y_meta_plane +
			uv_meta_plane);
		break;
	case V4L2_PIX_FMT_QC10C:
		y_stride =
			ALIGN(ALIGN(pix_width, 192) * 4 / 3, 256);
		uv_stride =
			ALIGN(ALIGN(pix_width, 192) * 4 / 3, 256);
		y_sclines =
			ALIGN(pix_height, 16);
		uv_sclines =
			ALIGN((pix_height + 1) >> 1, 16);
		y_ubwc_plane =
			ALIGN(y_stride * y_sclines, 4096);
		uv_ubwc_plane =
			ALIGN(uv_stride * uv_sclines, 4096);
		y_meta_stride =
			ALIGN(DIV_ROUND_UP(pix_width, 48), 64);
		y_meta_scanlines =
			ALIGN(DIV_ROUND_UP(pix_height, 4), 16);
		y_meta_plane =
			ALIGN(y_meta_stride * y_meta_scanlines, 4096);
		uv_meta_stride =
			ALIGN(DIV_ROUND_UP((pix_width + 1) >> 1, 24), 64);
		uv_meta_scanlines =
			ALIGN(DIV_ROUND_UP((pix_height + 1) >> 1, 4), 16);
		uv_meta_plane =
			ALIGN(uv_meta_stride * uv_meta_scanlines, 4096);

		size = y_ubwc_plane + uv_ubwc_plane + y_meta_plane +
			uv_meta_plane;
		break;
	default:
		break;
	}

invalid_input:
	size = ALIGN(size, 4096);

	return size;
}

static int input_min_count(struct iris_inst *inst)
{
	return MIN_BUFFERS;
}

static int output_min_count(struct iris_inst *inst)
{
	int output_min_count;

	switch (inst->codec) {
	case H264:
	case HEVC:
		output_min_count = 4;
		break;
	case VP9:
		output_min_count = 9;
		break;
	default:
		output_min_count = 4;
		break;
	}

	return output_min_count;
}

int iris_get_buf_min_count(struct iris_inst *inst,
			   enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return input_min_count(inst);
	case BUF_OUTPUT:
		return output_min_count(inst);
	default:
		return 0;
	}
}

static u32 input_buffer_size(struct iris_inst *inst)
{
	u32 base_res_mbs = NUM_MBS_4k;
	u32 frame_size, num_mbs;
	struct v4l2_format *f;
	u32 div_factor = 1;
	u32 codec;

	f = inst->fmt_src;
	codec = f->fmt.pix_mp.pixelformat;

	num_mbs = get_mbpf(inst);
	if (num_mbs > NUM_MBS_4k) {
		div_factor = 4;
		base_res_mbs = inst->cap[MBPF].value;
	} else {
		base_res_mbs = NUM_MBS_4k;
		if (codec == V4L2_PIX_FMT_VP9)
			div_factor = 1;
		else
			div_factor = 2;
	}

	frame_size = base_res_mbs * MB_IN_PIXEL * 3 / 2 / div_factor;

	 /* multiply by 10/8 (1.25) to get size for 10 bit case */
	if (codec == V4L2_PIX_FMT_VP9 || codec == V4L2_PIX_FMT_HEVC)
		frame_size = frame_size + (frame_size >> 2);

	return ALIGN(frame_size, SZ_4K);
}

static u32 output_buffer_size(struct iris_inst *inst)
{
	struct v4l2_format *f;
	u32 size;

	f = inst->fmt_dst;

	size = video_buffer_size(f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width,
				 f->fmt.pix_mp.height);
	return size;
}

int iris_get_buffer_size(struct iris_inst *inst,
			 enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return input_buffer_size(inst);
	case BUF_OUTPUT:
		return output_buffer_size(inst);
	default:
		return 0;
	}
}

struct iris_buffers *iris_get_buffer_list(struct iris_inst *inst,
					  enum iris_buffer_type buffer_type)
{
	switch (buffer_type) {
	case BUF_INPUT:
		return &inst->buffers.input;
	case BUF_OUTPUT:
		return &inst->buffers.output;
	case BUF_READ_ONLY:
		return &inst->buffers.read_only;
	case BUF_BIN:
		return &inst->buffers.bin;
	case BUF_ARP:
		return &inst->buffers.arp;
	case BUF_COMV:
		return &inst->buffers.comv;
	case BUF_NON_COMV:
		return &inst->buffers.non_comv;
	case BUF_LINE:
		return &inst->buffers.line;
	case BUF_DPB:
		return &inst->buffers.dpb;
	case BUF_PERSIST:
		return &inst->buffers.persist;
	case BUF_VPSS:
		return &inst->buffers.vpss;
	default:
		return NULL;
	}
}

int iris_allocate_buffers(struct iris_inst *inst,
			  enum iris_buffer_type buf_type,
			  u32 num_buffers)
{
	struct iris_buffer *buf = NULL;
	struct iris_buffers *buffers;
	int idx = 0;

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return -EINVAL;

	for (idx = 0; idx < num_buffers; idx++) {
		buf = iris_get_buffer_from_pool(inst);
		if (!buf)
			return -EINVAL;

		INIT_LIST_HEAD(&buf->list);
		list_add_tail(&buf->list, &buffers->list);
		buf->type = buf_type;
		buf->index = idx;
	}

	return 0;
}

int iris_free_buffers(struct iris_inst *inst,
		      enum iris_buffer_type buf_type)
{
	struct iris_buffer *buf, *dummy;
	struct iris_buffers *buffers;

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return -EINVAL;

	list_for_each_entry_safe(buf, dummy, &buffers->list, list) {
		list_del_init(&buf->list);
		iris_return_buffer_to_pool(inst, buf);
	}

	return 0;
}
