// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>

#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_helpers.h"
#include "iris_vdec.h"

static int vdec_codec_change(struct iris_inst *inst, u32 v4l2_codec)
{
	bool session_init = false;
	int ret;

	if (!inst->codec)
		session_init = true;

	if (inst->codec && inst->fmt_src->fmt.pix_mp.pixelformat == v4l2_codec)
		return 0;

	inst->codec = v4l2_codec_to_driver(inst, v4l2_codec);
	if (!inst->codec)
		return -EINVAL;

	inst->fmt_src->fmt.pix_mp.pixelformat = v4l2_codec;
	ret = get_inst_capability(inst);
	if (ret)
		return ret;

	ret = ctrls_init(inst, session_init);
	if (ret)
		return ret;

	ret = update_buffer_count(inst, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = update_buffer_count(inst, OUTPUT_MPLANE);

	return ret;
}

int vdec_inst_init(struct iris_inst *inst)
{
	struct v4l2_format *f;
	int ret;

	inst->fmt_src  = kzalloc(sizeof(*inst->fmt_src), GFP_KERNEL);
	inst->fmt_dst  = kzalloc(sizeof(*inst->fmt_dst), GFP_KERNEL);
	inst->vb2q_src = kzalloc(sizeof(*inst->vb2q_src), GFP_KERNEL);
	inst->vb2q_dst = kzalloc(sizeof(*inst->vb2q_dst), GFP_KERNEL);

	f = inst->fmt_src;
	f->type = INPUT_MPLANE;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;

	f = inst->fmt_dst;
	f->type = OUTPUT_MPLANE;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_QC08C;
	f->fmt.pix_mp.width = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.height = ALIGN(DEFAULT_HEIGHT, 32);
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
	inst->buffers.output.actual_count = inst->buffers.output.min_count;
	inst->buffers.output.size = f->fmt.pix_mp.plane_fmt[0].sizeimage;
	inst->fw_min_count = 0;

	ret = vdec_codec_change(inst, inst->fmt_src->fmt.pix_mp.pixelformat);

	return ret;
}

void vdec_inst_deinit(struct iris_inst *inst)
{
	kfree(inst->fmt_dst);
	kfree(inst->fmt_src);
}

static int vdec_check_colorformat_supported(struct iris_inst *inst,
					    enum colorformat_type colorformat)
{
	bool supported = true;

	if (!inst->vb2q_src->streaming)
		return true;

	if (inst->cap[BIT_DEPTH].value == BIT_DEPTH_8 &&
	    !is_8bit_colorformat(colorformat))
		supported = false;
	if (inst->cap[BIT_DEPTH].value == BIT_DEPTH_10 &&
	    !is_10bit_colorformat(colorformat))
		supported = false;
	if (inst->cap[CODED_FRAMES].value == CODED_FRAMES_INTERLACE)
		supported = false;

	return supported;
}

int vdec_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f)
{
	struct iris_core *core;
	u32 array[32] = {0};
	u32 i = 0;

	if (f->index >= ARRAY_SIZE(array))
		return -EINVAL;

	core = inst->core;
	if (f->type == INPUT_MPLANE) {
		u32 codecs = core->cap[DEC_CODECS].value;
		u32 codecs_count = hweight32(codecs);
		u32 idx = 0;

		for (i = 0; i <= codecs_count; i++) {
			if (codecs & BIT(i)) {
				if (idx >= ARRAY_SIZE(array))
					break;
				array[idx] = codecs & BIT(i);
				idx++;
			}
		}
		if (!array[f->index])
			return -EINVAL;
		f->pixelformat = v4l2_codec_from_driver(inst, array[f->index]);
		f->flags = V4L2_FMT_FLAG_COMPRESSED;
		strscpy(f->description, "codec", sizeof(f->description));
	} else if (f->type == OUTPUT_MPLANE) {
		u32 formats = inst->cap[PIX_FMTS].step_or_mask;
		u32 idx = 0;

		for (i = 0; i <= 31; i++) {
			if (formats & BIT(i)) {
				if (idx >= ARRAY_SIZE(array))
					break;
				if (vdec_check_colorformat_supported(inst, formats & BIT(i))) {
					array[idx] = formats & BIT(i);
					idx++;
				}
			}
		}
		if (!array[f->index])
			return -EINVAL;
		f->pixelformat = v4l2_colorformat_from_driver(inst, array[f->index]);
		strscpy(f->description, "colorformat", sizeof(f->description));
	}

	if (!f->pixelformat)
		return -EINVAL;

	memset(f->reserved, 0, sizeof(f->reserved));

	return 0;
}

int vdec_try_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_format *f_inst;
	u32 pix_fmt;

	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));
	if (f->type == INPUT_MPLANE) {
		pix_fmt = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
			pix_fmt = v4l2_codec_to_driver(inst, f->fmt.pix_mp.pixelformat);
		}
	} else if (f->type == OUTPUT_MPLANE) {
		pix_fmt = v4l2_colorformat_to_driver(inst, f->fmt.pix_mp.pixelformat);
		if (!pix_fmt) {
			f_inst = inst->fmt_dst;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
		}
		if (inst->vb2q_src->streaming) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
		}
	} else {
		return -EINVAL;
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;

	pixmp->num_planes = 1;

	return 0;
}

int vdec_s_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt, *output_fmt;
	enum colorformat_type colorformat;
	u32 codec_align, stride = 0;
	int ret = 0;

	vdec_try_fmt(inst, f);

	if (f->type == INPUT_MPLANE) {
		if (inst->fmt_src->fmt.pix_mp.pixelformat !=
			f->fmt.pix_mp.pixelformat) {
			ret = vdec_codec_change(inst, f->fmt.pix_mp.pixelformat);
			if (ret)
				return ret;
		}

		fmt = inst->fmt_src;
		fmt->type = INPUT_MPLANE;

		codec_align = inst->fmt_src->fmt.pix_mp.pixelformat ==
			V4L2_PIX_FMT_HEVC ? 32 : 16;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, codec_align);
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, codec_align);
		fmt->fmt.pix_mp.num_planes = 1;
		fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
		inst->buffers.input.min_count = iris_get_buf_min_count(inst, BUF_INPUT);
		if (inst->buffers.input.actual_count <
			inst->buffers.input.min_count) {
			inst->buffers.input.actual_count =
				inst->buffers.input.min_count;
		}
		inst->buffers.input.size =
			fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

		fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		output_fmt = inst->fmt_dst;
		output_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		output_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		output_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		output_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = f->fmt.pix_mp.width;
		inst->crop.height = f->fmt.pix_mp.height;
	} else if (f->type == OUTPUT_MPLANE) {
		fmt = inst->fmt_dst;
		fmt->type = OUTPUT_MPLANE;
		if (inst->vb2q_src->streaming) {
			f->fmt.pix_mp.height = inst->fmt_src->fmt.pix_mp.height;
			f->fmt.pix_mp.width = inst->fmt_src->fmt.pix_mp.width;
		}
		fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
		codec_align = f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C ? 192 : 128;
		fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, codec_align);
		codec_align = f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C ? 16 : 32;
		fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, codec_align);
		fmt->fmt.pix_mp.num_planes = 1;
		if (f->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_QC10C) {
			stride = ALIGN(f->fmt.pix_mp.width, 192);
			fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(stride * 4 / 3, 256);
		} else {
			fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(f->fmt.pix_mp.width, 128);
		}
		fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

		if (!inst->vb2q_src->streaming)
			inst->buffers.output.min_count = iris_get_buf_min_count(inst, BUF_OUTPUT);
		if (inst->buffers.output.actual_count <
			inst->buffers.output.min_count) {
			inst->buffers.output.actual_count =
				inst->buffers.output.min_count;
		}

		colorformat = v4l2_colorformat_to_driver(inst, fmt->fmt.pix_mp.pixelformat);
		inst->buffers.output.size =
			fmt->fmt.pix_mp.plane_fmt[0].sizeimage;
		inst->cap[PIX_FMTS].value = colorformat;

		if (!inst->vb2q_src->streaming) {
			inst->crop.top = 0;
			inst->crop.left = 0;
			inst->crop.width = f->fmt.pix_mp.width;
			inst->crop.height = f->fmt.pix_mp.height;
		}
	} else {
		return -EINVAL;
	}
	memcpy(f, fmt, sizeof(*fmt));

	return ret;
}

int vdec_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub)
{
	int ret = 0;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		ret = v4l2_event_subscribe(&inst->fh, sub, MAX_EVENTS, NULL);
		break;
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(&inst->fh, sub);
		break;
	case V4L2_EVENT_CTRL:
		ret = v4l2_ctrl_subscribe_event(&inst->fh, sub);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}
