// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_buffer.h"
#include "iris_ctrls.h"
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

	if (inst->state == IRIS_INST_STREAMING)
		return -EINVAL;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes ||
		    sizes[0] < f->fmt.pix_mp.plane_fmt[0].sizeimage)
			return -EINVAL;
	}

	buffer_type = v4l2_type_to_driver(q->type);
	if (!buffer_type)
		return -EINVAL;

	if (list_empty(&inst->caps_list)) {
		ret = prepare_dependency_list(inst);
		if (ret)
			return ret;
	}

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = adjust_v4l2_properties(inst);
		if (ret)
			return ret;
	}

	ret = check_session_supported(inst);
	if (ret)
		return ret;

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

void *iris_vb2_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
			     struct dma_buf *dbuf, unsigned long size)
{
	enum iris_buffer_type buf_type;
	struct iris_buffers *buffers;
	struct iris_buffer *iter;
	struct iris_buffer *buf;
	struct iris_inst *inst;
	bool found = false;

	if (!vb || !dev || !dbuf || !vb->vb2_queue)
		return ERR_PTR(-EINVAL);

	inst = vb->vb2_queue->drv_priv;

	buf_type = v4l2_type_to_driver(vb->type);

	buffers = iris_get_buffer_list(inst, buf_type);
	if (!buffers)
		return NULL;

	list_for_each_entry(iter, &buffers->list, list) {
		if (iter->index == vb->index) {
			found = true;
			buf = iter;
			break;
		}
	}

	if (!found)
		return NULL;

	buf->inst = inst;
	buf->dmabuf = dbuf;

	buf->attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR(buf->attach)) {
		buf->attach = NULL;
		return NULL;
	}

	return buf;
}

int iris_vb2_map_dmabuf(void *buf_priv)
{
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return -EINVAL;

	inst = buf->inst;
	core = inst->core;

	if (!buf->attach) {
		dev_err(core->dev, "trying to map a non attached buffer\n");
		return -EINVAL;
	}

	buf->sg_table = dma_buf_map_attachment(buf->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(buf->sg_table))
		return -EINVAL;

	if (!buf->sg_table->sgl) {
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
		return -EINVAL;
	}

	buf->device_addr = sg_dma_address(buf->sg_table->sgl);

	return 0;
}

void iris_vb2_unmap_dmabuf(void *buf_priv)
{
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return;

	inst = buf->inst;
	core = inst->core;

	if (!buf->attach) {
		dev_err(core->dev, "trying to unmap a non attached buffer\n");
		return;
	}

	if (!buf->sg_table) {
		dev_err(core->dev, "dmabuf buffer is already unmapped\n");
		return;
	}

	if (buf->attach && buf->sg_table) {
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
		buf->device_addr = 0x0;
	}
}

void iris_vb2_detach_dmabuf(void *buf_priv)
{
	struct iris_buffer *buf = buf_priv;
	struct iris_core *core;
	struct iris_inst *inst;

	if (!buf || !buf->inst)
		return;

	inst = buf->inst;
	core = inst->core;

	if (buf->sg_table) {
		dev_err(core->dev, "trying to detach an unmapped buffer\n");
		dma_buf_unmap_attachment(buf->attach, buf->sg_table, DMA_BIDIRECTIONAL);
		buf->sg_table = NULL;
	}

	if (buf->attach && buf->dmabuf) {
		dma_buf_detach(buf->dmabuf, buf->attach);
		buf->attach = NULL;
	}

	buf->dmabuf = NULL;
	buf->inst = NULL;
}

void *iris_vb2_alloc(struct vb2_buffer *vb, struct device *dev,
		     unsigned long size)
{
	return (void *)0xdeadbeef;
}

void iris_vb2_put(void *buf_priv)
{
}

int iris_vb2_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	return 0;
}
