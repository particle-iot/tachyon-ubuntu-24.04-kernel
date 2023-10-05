// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/list.h>

#include "platform_common.h"
#include "iris_helpers.h"
#include "iris_hfi_packet.h"
#include "iris_hfi.h"
#include "iris_ctrls.h"

#define MIN_CAPTURE_BUFFERS 4
#define MIN_OUTPUT_BUFFERS 4

static inline bool is_all_childrens_visited(struct plat_inst_cap *cap,
					    bool lookup[INST_CAP_MAX])
{
	bool found = true;
	int i;

	for (i = 0; i < MAX_NUM_CHILD; i++) {
		if (cap->children[i] == INST_CAP_NONE)
			continue;

		if (!lookup[cap->children[i]]) {
			found = false;
			break;
		}
	}

	return found;
}

static bool is_valid_cap_id(enum plat_inst_cap_type cap_id)
{
	return cap_id > INST_CAP_NONE && cap_id < INST_CAP_MAX;
}

static enum plat_inst_cap_type get_cap_id(struct iris_inst *inst, u32 id)
{
	enum plat_inst_cap_type iter = INST_CAP_NONE;
	enum plat_inst_cap_type cap_id = INST_CAP_NONE;

	do {
		if (inst->cap[iter].v4l2_id == id) {
			cap_id = inst->cap[iter].cap_id;
			break;
		}
		iter++;
	} while (iter < INST_CAP_MAX);

	return cap_id;
}

static int add_node_list(struct list_head *list, enum plat_inst_cap_type cap_id)
{
	struct cap_entry *entry = NULL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->list);
	entry->cap_id = cap_id;
	list_add(&entry->list, list);

	return 0;
}

static int add_children(struct list_head *list,
			struct iris_inst *inst,
			enum plat_inst_cap_type cap_id)
{
	struct plat_inst_cap *cap;
	int i, ret = 0;

	cap = &inst->cap[cap_id];

	for (i = 0; i < MAX_NUM_CHILD; i++) {
		if (!cap->children[i])
			break;

		if (!is_valid_cap_id(cap->children[i]))
			continue;

		ret = add_node_list(list, cap->children[i]);
		if (ret)
			return ret;
	}

	return ret;
}

static int adjust_cap(struct iris_inst *inst,
		      enum plat_inst_cap_type cap_id,
		      struct v4l2_ctrl *ctrl)
{
	struct plat_inst_cap *cap;

	cap = &inst->cap[cap_id];
	if (!inst->cap[cap_id].cap_id)
		return 0;

	if (!cap->adjust) {
		if (ctrl)
			inst->cap[cap_id].value = ctrl->val;
		return 0;
	}

	return cap->adjust(inst, ctrl);
}

static int adjust_dynamic_property(struct iris_inst *inst,
				   enum plat_inst_cap_type cap_id,
				   struct v4l2_ctrl *ctrl,
				   struct list_head *firmware_list,
				   struct list_head *children_list,
				   bool cap_present[INST_CAP_MAX])
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap;
	s32 prev_value;
	int ret;

	cap = &inst->cap[0];

	if (!(cap[cap_id].flags & CAP_FLAG_DYNAMIC_ALLOWED))
		return -EBUSY;

	prev_value = cap[cap_id].value;
	ret = adjust_cap(inst, cap_id, ctrl);
	if (ret)
		return ret;

	ret = add_node_list(firmware_list, cap_id);
	if (ret)
		return ret;
	cap_present[cap->cap_id] = true;

	if (cap[cap_id].value == prev_value)
		return 0;

	ret = add_children(children_list, inst, cap_id);
	if (ret)
		return ret;

	list_for_each_entry_safe(entry, temp, children_list, list) {
		if (!cap[entry->cap_id].adjust) {
			list_del_init(&entry->list);
			kfree(entry);
			continue;
		}

		prev_value = cap[entry->cap_id].value;
		ret = adjust_cap(inst, entry->cap_id, NULL);
		if (ret)
			return ret;

		if (cap[entry->cap_id].value != prev_value) {
			if (!cap_present[cap->cap_id]) {
				ret = add_node_list(firmware_list, cap_id);
				if (ret)
					return ret;
				cap_present[cap->cap_id] = true;
			}

			ret = add_children(children_list, inst, entry->cap_id);
			if (ret)
				return ret;
		}

		list_del_init(&entry->list);
		kfree(entry);
	}

	if (!list_empty(children_list))
		return -EINVAL;

	return ret;
}

static int set_dynamic_property(struct iris_inst *inst,
				struct list_head *firmware_list)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap;
	int ret = 0;

	list_for_each_entry_safe(entry, temp, firmware_list, list) {
		cap = &inst->cap[entry->cap_id];
		if (cap->set) {
			ret = cap->set(inst, entry->cap_id);
			if (ret)
				return -EINVAL;
		}
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

static int vdec_op_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	enum plat_inst_cap_type cap_id;
	struct iris_inst *inst = NULL;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctrl->val = inst->buffers.output.min_count;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ctrl->val = inst->buffers.input.min_count;
		break;
	default:
		inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
		cap_id = get_cap_id(inst, ctrl->id);
		if (is_valid_cap_id(cap_id))
			ctrl->val = inst->cap[cap_id].value;
		else
			ret = -EINVAL;
	}

	return ret;
}

static int vdec_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct list_head children_list, firmware_list;
	struct ctrl_data *priv_ctrl_data;
	enum plat_inst_cap_type cap_id;
	bool cap_present[INST_CAP_MAX];
	struct plat_inst_cap *cap;
	struct iris_inst *inst;
	int ret = 0;

	priv_ctrl_data = ctrl->priv ? ctrl->priv : NULL;
	if (priv_ctrl_data && priv_ctrl_data->skip_s_ctrl)
		return 0;

	inst = container_of(ctrl->handler, struct iris_inst, ctrl_handler);
	cap = &inst->cap[0];

	INIT_LIST_HEAD(&firmware_list);
	INIT_LIST_HEAD(&children_list);
	memset(&cap_present, 0, sizeof(cap_present));

	cap_id = get_cap_id(inst, ctrl->id);
	if (!is_valid_cap_id(cap_id))
		return -EINVAL;

	if (!allow_s_ctrl(inst, cap_id))
		return -EBUSY;

	cap[cap_id].flags |= CAP_FLAG_CLIENT_SET;

	if (!inst->vb2q_src->streaming) {
		inst->cap[cap_id].value = ctrl->val;
	} else {
		ret = adjust_dynamic_property(inst, cap_id, ctrl,
					      &firmware_list, &children_list,
					      cap_present);
		if (ret)
			goto free_list;

		ret = set_dynamic_property(inst, &firmware_list);
	}

free_list:
	list_for_each_entry_safe(entry, temp, &children_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	list_for_each_entry_safe(entry, temp, &firmware_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops = {
	.s_ctrl = vdec_op_s_ctrl,
	.g_volatile_ctrl = vdec_op_g_volatile_ctrl,
};

int ctrls_init(struct iris_inst *inst, bool init)
{
	int num_ctrls = 0, ctrl_idx = 0;
	struct plat_inst_cap *cap;
	struct iris_core *core;
	u64 step_or_mask;
	int idx = 0;
	int ret = 0;

	core = inst->core;
	cap = &inst->cap[0];

	for (idx = 0; idx < INST_CAP_MAX; idx++) {
		if (cap[idx].v4l2_id)
			num_ctrls++;
	}
	if (!num_ctrls)
		return -EINVAL;

	if (init) {
		ret = v4l2_ctrl_handler_init(&inst->ctrl_handler,
					     INST_CAP_MAX * core->dec_codecs_count);
		if (ret)
			return ret;
	}

	for (idx = 0; idx < INST_CAP_MAX; idx++) {
		struct v4l2_ctrl *ctrl;

		if (!cap[idx].v4l2_id)
			continue;

		if (ctrl_idx >= num_ctrls) {
			ret = -EINVAL;
			goto error;
		}

		if (!init) {
			struct ctrl_data ctrl_priv_data;

			ctrl = v4l2_ctrl_find(&inst->ctrl_handler, cap[idx].v4l2_id);
			if (ctrl) {
				step_or_mask = (cap[idx].flags & CAP_FLAG_MENU) ?
					~(cap[idx].step_or_mask) :
					cap[idx].step_or_mask;
				memset(&ctrl_priv_data, 0, sizeof(ctrl_priv_data));
				ctrl_priv_data.skip_s_ctrl = true;
				ctrl->priv = &ctrl_priv_data;
				v4l2_ctrl_modify_range(ctrl,
						       cap[idx].min,
						       cap[idx].max,
						       step_or_mask,
						       cap[idx].value);
				ctrl->priv = NULL;
				continue;
			}
		}

		if (cap[idx].flags & CAP_FLAG_MENU) {
			ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler,
						      &ctrl_ops,
						      cap[idx].v4l2_id,
						      cap[idx].max,
						      ~(cap[idx].step_or_mask),
						      cap[idx].value);
		} else {
			ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler,
						 &ctrl_ops,
						 cap[idx].v4l2_id,
						 cap[idx].min,
						 cap[idx].max,
						 cap[idx].step_or_mask,
						 cap[idx].value);
		}
		if (!ctrl) {
			ret = -EINVAL;
			goto error;
		}

		ret = inst->ctrl_handler.error;
		if (ret)
			goto error;

		if ((cap[idx].flags & CAP_FLAG_VOLATILE) ||
		    (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE ||
		     ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_OUTPUT))
			ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

		ctrl->flags |= V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		ctrl_idx++;
	}
	inst->num_ctrls = num_ctrls;

	return 0;
error:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);

	return ret;
}

int iris_init_core_caps(struct iris_core *core)
{
	struct plat_core_cap *core_platform_data;
	int i, num_core_caps;

	core_platform_data = core->platform_data->core_data;
	if (!core_platform_data)
		return -EINVAL;

	num_core_caps = core->platform_data->core_data_size;

	for (i = 0; i < num_core_caps && i < CORE_CAP_MAX; i++) {
		core->cap[core_platform_data[i].type].type = core_platform_data[i].type;
		core->cap[core_platform_data[i].type].value = core_platform_data[i].value;
	}

	return 0;
}

static int update_inst_capability(struct plat_inst_cap *in,
				  struct plat_inst_caps *capability)
{
	if (!in || !capability)
		return -EINVAL;

	if (in->cap_id >= INST_CAP_MAX)
		return -EINVAL;

	capability->cap[in->cap_id].cap_id = in->cap_id;
	capability->cap[in->cap_id].min = in->min;
	capability->cap[in->cap_id].max = in->max;
	capability->cap[in->cap_id].step_or_mask = in->step_or_mask;
	capability->cap[in->cap_id].value = in->value;
	capability->cap[in->cap_id].flags = in->flags;
	capability->cap[in->cap_id].v4l2_id = in->v4l2_id;
	capability->cap[in->cap_id].hfi_id = in->hfi_id;
	memcpy(capability->cap[in->cap_id].children, in->children,
	       sizeof(capability->cap[in->cap_id].children));
	capability->cap[in->cap_id].adjust = in->adjust;
	capability->cap[in->cap_id].set = in->set;

	return 0;
}

int iris_init_instance_caps(struct iris_core *core)
{
	struct plat_inst_cap *inst_plat_cap_data = NULL;
	u8 dec_codecs_count = 0;
	int num_inst_cap;
	u32 dec_valid_codecs;
	int i, j, check_bit = 0;
	int ret = 0;

	inst_plat_cap_data = core->platform_data->inst_cap_data;
	if (!inst_plat_cap_data)
		return -EINVAL;

	dec_valid_codecs = core->cap[DEC_CODECS].value;
	dec_codecs_count = hweight32(dec_valid_codecs);
	core->dec_codecs_count = dec_codecs_count;

	core->inst_caps = devm_kzalloc(core->dev,
				       dec_codecs_count * sizeof(struct plat_inst_caps),
				       GFP_KERNEL);
	if (!core->inst_caps)
		return -ENOMEM;

	for (i = 0; i < dec_codecs_count; i++) {
		while (check_bit < (sizeof(dec_valid_codecs) * 8)) {
			if (dec_valid_codecs & BIT(check_bit)) {
				core->inst_caps[i].codec = dec_valid_codecs &
						BIT(check_bit);
				check_bit++;
				break;
			}
			check_bit++;
		}
	}

	num_inst_cap = core->platform_data->inst_cap_data_size;

	for (i = 0; i < num_inst_cap; i++) {
		for (j = 0; j < dec_codecs_count; j++) {
			if ((inst_plat_cap_data[i].codec &
				core->inst_caps[j].codec)) {
				ret = update_inst_capability(&inst_plat_cap_data[i],
							     &core->inst_caps[j]);
				if (ret)
					return ret;
			}
		}
	}

	return ret;
}

int get_inst_capability(struct iris_inst *inst)
{
	struct iris_core *core;
	int i;

	core = inst->core;

	for (i = 0; i < core->dec_codecs_count; i++) {
		if (core->inst_caps[i].codec == inst->codec) {
			memcpy(&inst->cap[0], &core->inst_caps[i].cap[0],
			       (INST_CAP_MAX + 1) * sizeof(struct plat_inst_cap));
		}
	}

	return 0;
}

int prepare_dependency_list(struct iris_inst *inst)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	struct plat_inst_cap *cap, *temp_cap;
	int caps_to_prepare, pending_list_counter,
		pending_at_start = 0;
	struct list_head prepared_list, pending_list;
	bool is_prepared[INST_CAP_MAX];
	bool is_pending[INST_CAP_MAX];
	int i, ret = 0;

	cap = &inst->cap[0];

	if (!list_empty(&inst->caps_list))
		return 0;

	INIT_LIST_HEAD(&prepared_list);
	INIT_LIST_HEAD(&pending_list);
	memset(&is_prepared, 0, sizeof(is_prepared));
	memset(&is_pending, 0, sizeof(is_pending));

	for (i = 1; i < INST_CAP_MAX; i++) {
		temp_cap = &cap[i];
		if (!is_valid_cap_id(temp_cap->cap_id))
			continue;

		if (!temp_cap->children[0]) {
			if (!is_prepared[temp_cap->cap_id]) {
				ret = add_node_list(&prepared_list, temp_cap->cap_id);
				if (ret)
					goto free_list;
				is_prepared[temp_cap->cap_id] = true;
			}
		} else {
			if (!is_pending[temp_cap->cap_id]) {
				ret = add_node_list(&pending_list, temp_cap->cap_id);
				if (ret)
					goto free_list;
				is_pending[temp_cap->cap_id] = true;
			}
		}
	}

	list_for_each_entry(entry, &pending_list, list)
		pending_at_start++;

	caps_to_prepare = pending_at_start;
	pending_list_counter = pending_at_start;

	list_for_each_entry_safe(entry, temp, &pending_list, list) {
		list_del_init(&entry->list);
		is_pending[entry->cap_id] = false;
		pending_list_counter--;
		temp_cap = &cap[entry->cap_id];

		if (is_all_childrens_visited(temp_cap, is_prepared)) {
			list_add(&entry->list, &prepared_list);
			is_prepared[entry->cap_id] = true;
			caps_to_prepare--;
		} else {
			list_add_tail(&entry->list, &pending_list);
			is_pending[entry->cap_id] = true;
		}

		if (!pending_list_counter) {
			if (pending_at_start == caps_to_prepare) {
				ret = -EINVAL;
				goto free_list;
			}
			pending_at_start = caps_to_prepare;
			pending_list_counter = caps_to_prepare;
		}
	}

	if (!list_empty(&pending_list)) {
		ret = -EINVAL;
		goto free_list;
	}

	list_replace_init(&prepared_list, &inst->caps_list);

free_list:
	list_for_each_entry_safe(entry, temp, &pending_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}
	list_for_each_entry_safe(entry, temp, &prepared_list, list) {
		list_del_init(&entry->list);
		kfree(entry);
	}

	return ret;
}

int set_u32_enum(struct iris_inst *inst,
		 enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32_ENUM,
				     &hfi_value, sizeof(u32));
}

int set_u32(struct iris_inst *inst,
	    enum plat_inst_cap_type cap_id)
{
	u32 hfi_value = inst->cap[cap_id].value;
	u32 hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &hfi_value, sizeof(u32));
}

int set_stage(struct iris_inst *inst,
	      enum plat_inst_cap_type cap_id)
{
	struct v4l2_format *inp_f;
	u32 work_mode = STAGE_2;
	u32 width, height;
	u32 hfi_id;

	hfi_id = inst->cap[cap_id].hfi_id;

	inp_f = inst->fmt_src;
	height = inp_f->fmt.pix_mp.height;
	width = inp_f->fmt.pix_mp.width;
	if (res_is_less_than(width, height, 1280, 720))
		work_mode = STAGE_1;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &work_mode, sizeof(u32));
}

int set_pipe(struct iris_inst *inst,
	     enum plat_inst_cap_type cap_id)
{
	u32 work_route, hfi_id;

	work_route = inst->cap[PIPE].value;
	hfi_id = inst->cap[cap_id].hfi_id;

	return iris_hfi_set_property(inst, hfi_id, HFI_HOST_FLAGS_NONE,
				     get_port_info(inst, cap_id),
				     HFI_PAYLOAD_U32,
				     &work_route, sizeof(u32));
}

int adjust_v4l2_properties(struct iris_inst *inst)
{
	struct cap_entry *entry = NULL, *temp = NULL;
	int ret = 0;

	list_for_each_entry_safe(entry, temp, &inst->caps_list, list) {
		ret = adjust_cap(inst, entry->cap_id, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

int adjust_output_order(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	s32 display_delay = -1, display_delay_enable = -1;
	u32 adjusted_value;

	adjusted_value = ctrl ? ctrl->val :
		inst->cap[OUTPUT_ORDER].value;

	display_delay = inst->cap[DISPLAY_DELAY].value;
	display_delay_enable = inst->cap[DISPLAY_DELAY_ENABLE].value;

	if (display_delay_enable && !display_delay)
		adjusted_value = 1;

	inst->cap[OUTPUT_ORDER].value = adjusted_value;

	return 0;
}

int adjust_profile(struct iris_inst *inst, struct v4l2_ctrl *ctrl)
{
	u32 adjusted_value;
	s32 pix_fmt = -1;

	adjusted_value = ctrl ? ctrl->val :
		inst->cap[PROFILE].value;

	pix_fmt = inst->cap[PIX_FMTS].value;

	if (pix_fmt == FMT_TP10C)
		adjusted_value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10;
	else
		adjusted_value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;

	inst->cap[PROFILE].value = adjusted_value;

	return 0;
}
