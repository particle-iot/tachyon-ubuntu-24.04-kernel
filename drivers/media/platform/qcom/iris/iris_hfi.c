// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "firmware.h"
#include "hfi_defines.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_response.h"
#include "vpu_common.h"

static bool __valdiate_session(struct iris_core *core,
			       struct iris_inst *inst)
{
	struct iris_inst *temp;
	bool valid = false;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return false;

	list_for_each_entry(temp, &core->instances, list) {
		if (temp == inst) {
			valid = true;
			break;
		}
	}

	return valid;
}

static int sys_init(struct iris_core *core)
{
	int ret;

	ret = hfi_packet_sys_init(core, core->packet, core->packet_size);
	if (ret)
		return ret;

	ret = iris_hfi_queue_cmd_write(core, core->packet);

	return ret;
}

static int sys_image_version(struct iris_core *core)
{
	int ret;

	ret = hfi_packet_image_version(core, core->packet, core->packet_size);
	if (ret)
		return ret;

	ret = iris_hfi_queue_cmd_write(core, core->packet);

	return ret;
}

int iris_hfi_core_init(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	ret = iris_hfi_queue_init(core);
	if (ret)
		goto error;

	ret = iris_fw_load(core);
	if (ret)
		goto error;

	ret = call_vpu_op(core, boot_firmware, core);
	if (ret)
		goto error;

	ret = sys_init(core);
	if (ret)
		goto error;

	ret = sys_image_version(core);
	if (ret)
		goto error;

	return ret;

error:
	dev_err(core->dev, "%s(): failed\n", __func__);

	return ret;
}

int iris_hfi_core_deinit(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (core->state == IRIS_CORE_DEINIT)
		return 0;

	iris_fw_unload(core);

	return ret;
}

int iris_hfi_session_open(struct iris_inst *inst)
{
	struct iris_core *core;
	int ret;

	inst->packet_size = 4096;
	inst->packet = kzalloc(inst->packet_size, GFP_KERNEL);
	if (!inst->packet)
		return -ENOMEM;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto fail_free_packet;
	}

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_OPEN,
					 HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED,
					 HFI_PORT_NONE,
					 0,
					 HFI_PAYLOAD_U32,
					 &inst->session_id,
					 sizeof(u32));
	if (ret)
		goto fail_free_packet;

	ret = iris_hfi_queue_cmd_write(core, inst->packet);
	if (ret)
		goto fail_free_packet;

	mutex_unlock(&core->lock);

	return ret;

fail_free_packet:
	kfree(inst->packet);
	inst->packet = NULL;
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_session_close(struct iris_inst *inst)
{
	struct iris_core *core;
	int ret;

	if (!inst->packet)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->lock);

	if (!__valdiate_session(core, inst)) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = hfi_packet_session_command(inst,
					 HFI_CMD_CLOSE,
					 (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
					 HFI_HOST_FLAGS_INTR_REQUIRED |
					 HFI_HOST_FLAGS_NON_DISCARDABLE),
					 HFI_PORT_NONE,
					 inst->session_id,
					 HFI_PAYLOAD_NONE,
					 NULL,
					 0);
	if (ret)
		goto unlock;

	ret = iris_hfi_queue_cmd_write(inst->core, inst->packet);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

irqreturn_t iris_hfi_isr(int irq, void *data)
{
	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

irqreturn_t iris_hfi_isr_handler(int irq, void *data)
{
	struct iris_core *core = data;

	if (!core)
		return IRQ_NONE;

	mutex_lock(&core->lock);
	call_vpu_op(core, clear_interrupt, core);
	mutex_unlock(&core->lock);

	__response_handler(core);

	if (!call_vpu_op(core, watchdog, core, core->intr_status))
		enable_irq(irq);

	return IRQ_HANDLED;
}
