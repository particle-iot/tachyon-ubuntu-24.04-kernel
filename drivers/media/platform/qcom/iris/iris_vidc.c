// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_common.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_instance.h"
#include "iris_vdec.h"
#include "iris_vidc.h"
#include "iris_ctrls.h"
#include "memory.h"

static int vidc_v4l2_fh_init(struct iris_inst *inst)
{
	struct iris_core *core;

	core = inst->core;

	if (inst->fh.vdev)
		return -EINVAL;

	v4l2_fh_init(&inst->fh, core->vdev_dec);
	inst->fh.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->fh);

	return 0;
}

static int vidc_v4l2_fh_deinit(struct iris_inst *inst)
{
	if (!inst->fh.vdev)
		return 0;

	v4l2_fh_del(&inst->fh);
	inst->fh.ctrl_handler = NULL;
	v4l2_fh_exit(&inst->fh);

	return 0;
}

static int vb2q_init(struct iris_inst *inst,
		     struct vb2_queue *q, enum v4l2_buf_type type)
{
	struct iris_core *core;

	core = inst->core;

	q->lock = &inst->ctx_q_lock;
	q->type = type;
	q->io_modes = VB2_MMAP | VB2_DMABUF;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	q->ops = core->vb2_ops;
	q->mem_ops = core->vb2_mem_ops;
	q->drv_priv = inst;
	q->copy_timestamp = 1;
	q->min_queued_buffers = 0;
	return vb2_queue_init(q);
}

static int vidc_vb2_queue_init(struct iris_inst *inst)
{
	int ret;

	ret = vb2q_init(inst, inst->vb2q_src, INPUT_MPLANE);
	if (ret)
		return ret;

	ret = vb2q_init(inst, inst->vb2q_dst, OUTPUT_MPLANE);
	if (ret)
		goto fail_vb2q_src_deinit;

	return ret;

fail_vb2q_src_deinit:
	vb2_queue_release(inst->vb2q_src);

	return ret;
}

static int vidc_vb2_queue_deinit(struct iris_inst *inst)
{
	vb2_queue_release(inst->vb2q_src);
	kfree(inst->vb2q_src);
	inst->vb2q_src = NULL;

	vb2_queue_release(inst->vb2q_dst);
	kfree(inst->vb2q_dst);
	inst->vb2q_dst = NULL;

	return 0;
}

static int vidc_add_session(struct iris_inst *inst)
{
	struct iris_core *core;
	struct iris_inst *i;
	u32 count = 0;
	int ret = 0;

	core = inst->core;

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT) {
		ret = -EINVAL;
		goto unlock;
	}
	list_for_each_entry(i, &core->instances, list)
		count++;

	if (count < core->cap[MAX_SESSION_COUNT].value)
		list_add_tail(&inst->list, &core->instances);
	else
		ret = -EAGAIN;
unlock:
	mutex_unlock(&core->lock);

	return ret;
}

static int vidc_remove_session(struct iris_inst *inst)
{
	struct iris_inst *i, *temp;
	struct iris_core *core;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry_safe(i, temp, &core->instances, list) {
		if (i->session_id == inst->session_id) {
			list_del_init(&i->list);
			break;
		}
	}
	mutex_unlock(&core->lock);

	return 0;
}

static struct iris_inst *get_vidc_inst(struct file *filp, void *fh)
{
	if (!filp || !filp->private_data)
		return NULL;

	return container_of(filp->private_data,
					struct iris_inst, fh);
}

int vidc_open(struct file *filp)
{
	struct iris_core *core = video_drvdata(filp);
	struct iris_inst *inst = NULL;
	int i = 0;
	int ret;

	ret = iris_core_init(core);
	if (ret)
		return ret;

	ret = iris_core_init_wait(core);
	if (ret)
		return ret;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->core = core;
	inst->session_id = hash32_ptr(inst);
	mutex_init(&inst->ctx_q_lock);
	for (i = 0; i < MAX_SIGNAL; i++)
		init_completion(&inst->completions[i]);

	ret = vidc_add_session(inst);
	if (ret)
		goto fail_free_inst;

	ret = iris_mem_pool_init(inst);
	if (ret)
		goto fail_remove_session;

	INIT_LIST_HEAD(&inst->caps_list);
	for (i = 0; i < MAX_SIGNAL; i++)
		init_completion(&inst->completions[i]);

	ret = vidc_v4l2_fh_init(inst);
	if (ret)
		goto fail_mem_pool_deinit;

	ret = vdec_inst_init(inst);
	if (ret)
		goto fail_fh_deinit;

	ret = vidc_vb2_queue_init(inst);
	if (ret)
		goto fail_inst_deinit;

	ret = get_inst_capability(inst);
	if (ret)
		goto fail_queue_deinit;

	ret = ctrls_init(inst);
	if (ret)
		goto fail_queue_deinit;

	ret = iris_hfi_session_open(inst);
	if (ret) {
		dev_err(core->dev, "%s: session open failed\n", __func__);
		goto fail_core_deinit;
	}
	filp->private_data = &inst->fh;

	return 0;

fail_core_deinit:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	iris_core_deinit(core);
fail_queue_deinit:
	vidc_vb2_queue_deinit(inst);
fail_inst_deinit:
	vdec_inst_deinit(inst);
fail_fh_deinit:
	vidc_v4l2_fh_deinit(inst);
fail_mem_pool_deinit:
	iris_mem_pool_deinit(inst);
fail_remove_session:
	vidc_remove_session(inst);
fail_free_inst:
	mutex_destroy(&inst->ctx_q_lock);
	kfree(inst);

	return ret;
}

int vidc_close(struct file *filp)
{
	struct iris_inst *inst;

	inst = get_vidc_inst(filp, NULL);
	if (!inst)
		return -EINVAL;

	v4l2_ctrl_handler_free(&inst->ctrl_handler);
	vdec_inst_deinit(inst);
	close_session(inst);
	vidc_vb2_queue_deinit(inst);
	vidc_v4l2_fh_deinit(inst);
	vidc_remove_session(inst);
	mutex_destroy(&inst->ctx_q_lock);
	kfree(inst);

	filp->private_data = NULL;

	return 0;
}

static __poll_t get_poll_flags(struct iris_inst *inst, u32 plane)
{
	struct vb2_buffer *vb = NULL;
	struct vb2_queue *q = NULL;
	unsigned long flags = 0;
	__poll_t poll = 0;

	if (plane == INPUT_MPLANE)
		q = inst->vb2q_src;
	else if (plane == OUTPUT_MPLANE)
		q = inst->vb2q_dst;

	if (!q)
		return EPOLLERR;

	spin_lock_irqsave(&q->done_lock, flags);
	if (!list_empty(&q->done_list))
		vb = list_first_entry(&q->done_list, struct vb2_buffer,
				      done_entry);
	if (vb && (vb->state == VB2_BUF_STATE_DONE ||
		   vb->state == VB2_BUF_STATE_ERROR)) {
		if (plane == OUTPUT_MPLANE)
			poll |= EPOLLIN | EPOLLRDNORM;
		else if (plane == INPUT_MPLANE)
			poll |= EPOLLOUT | EPOLLWRNORM;
	}
	spin_unlock_irqrestore(&q->done_lock, flags);

	return poll;
}

static __poll_t vidc_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct iris_inst *inst;
	__poll_t poll = 0;

	inst = get_vidc_inst(filp, NULL);
	if (!inst)
		return EPOLLERR;

	poll_wait(filp, &inst->fh.wait, pt);
	poll_wait(filp, &inst->vb2q_src->done_wq, pt);
	poll_wait(filp, &inst->vb2q_dst->done_wq, pt);

	if (v4l2_event_pending(&inst->fh))
		poll |= EPOLLPRI;

	poll |= get_poll_flags(inst, INPUT_MPLANE);
	poll |= get_poll_flags(inst, OUTPUT_MPLANE);

	return poll;
}

static const struct v4l2_file_operations v4l2_file_ops = {
	.owner                          = THIS_MODULE,
	.open                           = vidc_open,
	.release                        = vidc_close,
	.unlocked_ioctl                 = video_ioctl2,
	.poll                           = vidc_poll,
};

int init_ops(struct iris_core *core)
{
	core->v4l2_file_ops = &v4l2_file_ops;

	return 0;
}
