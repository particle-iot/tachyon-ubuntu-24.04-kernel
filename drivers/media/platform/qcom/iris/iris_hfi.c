// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "firmware.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_packet.h"
#include "vpu_common.h"

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
