// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_instance.h"
#include "iris_vb2.h"

int iris_vb2_queue_setup(struct vb2_queue *q,
			 unsigned int *num_buffers, unsigned int *num_planes,
			 unsigned int sizes[], struct device *alloc_devs[])
{
	enum iris_buffer_type buffer_type = 0;
	struct iris_buffers *buffers;
	struct iris_inst *inst;
	struct iris_core *core;
	struct v4l2_format *f;
	int ret;

	if (!q || !num_buffers || !num_planes || !sizes)
		return -EINVAL;

	inst = vb2_get_drv_priv(q);
	if (!inst || !inst->core)
		return -EINVAL;

	core = inst->core;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		f = inst->fmt_src;
	else
		f = inst->fmt_dst;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage)
			return -EINVAL;
	}

	buffer_type = v4l2_type_to_driver(q->type);
	if (!buffer_type)
		return -EINVAL;

	ret = iris_free_buffers(inst, buffer_type);
	if (ret)
		return ret;

	buffers = iris_get_buffer_list(inst, buffer_type);
	if (!buffers)
		return -EINVAL;

	buffers->min_count = iris_get_buf_min_count(inst, buffer_type);
	if (*num_buffers < buffers->min_count)
		*num_buffers = buffers->min_count;
	buffers->actual_count = *num_buffers;
	*num_planes = 1;

	buffers->size = iris_get_buffer_size(inst, buffer_type);

	f->fmt.pix_mp.plane_fmt[0].sizeimage = buffers->size;
	sizes[0] = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	ret = iris_allocate_buffers(inst, buffer_type, *num_buffers);

	q->dev = core->dev;

	return ret;
}
