// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>

#include "iris_core.h"
#include "iris_firmware.h"
#include "iris_state.h"
#include "iris_vpu_common.h"

void iris_core_deinit(struct iris_core *core)
{
	int ret;

	/*
	 * A failed resume must not be followed by a put: the helper has
	 * already dropped the reference it took.
	 *
	 * The teardown itself splits in two. iris_fw_unload() is a SCM call
	 * (qcom_scm_pas_shutdown()) and does not touch Iris registers, so it
	 * must run either way - leaving the PAS loaded while the driver
	 * declares itself deinitialised would strand firmware state across
	 * both the remove path and the system-error path, which re-inits the
	 * core straight away. iris_vpu_power_off() does touch registers and
	 * clocks, so it only runs when the device is actually resumed; the
	 * runtime-PM resume failure path has already removed VPU power.
	 */
	ret = pm_runtime_resume_and_get(core->dev);
	if (ret < 0)
		dev_err(core->dev, "deinit without a resumed device: %d\n", ret);

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_DEINIT) {
		iris_fw_unload(core);
		if (ret >= 0)
			iris_vpu_power_off(core);
		iris_hfi_queues_deinit(core);
		core->state = IRIS_CORE_DEINIT;
	}
	mutex_unlock(&core->lock);

	if (ret >= 0)
		pm_runtime_put_sync(core->dev);
}

static int iris_wait_for_system_response(struct iris_core *core)
{
	u32 hw_response_timeout_val = core->iris_platform_data->hw_response_timeout;
	int ret;

	if (core->state == IRIS_CORE_ERROR)
		return -EIO;

	ret = wait_for_completion_timeout(&core->core_init_done,
					  msecs_to_jiffies(hw_response_timeout_val));
	if (!ret) {
		core->state = IRIS_CORE_ERROR;
		return -ETIMEDOUT;
	}

	return 0;
}

int iris_core_init(struct iris_core *core)
{
	int ret;

	mutex_lock(&core->lock);
	if (core->state == IRIS_CORE_INIT) {
		ret = 0;
		goto exit;
	} else if (core->state == IRIS_CORE_ERROR) {
		ret = -EINVAL;
		goto error;
	}

	core->state = IRIS_CORE_INIT;

	ret = iris_hfi_queues_init(core);
	if (ret)
		goto error;

	ret = iris_vpu_power_on(core);
	if (ret)
		goto error_queue_deinit;

	ret = iris_fw_load(core);
	if (ret)
		goto error_power_off;

	ret = iris_vpu_boot_firmware(core);
	if (ret)
		goto error_unload_fw;

	ret = iris_vpu_switch_to_hwmode(core);
	if (ret)
		goto error_unload_fw;

	ret = iris_hfi_core_init(core);
	if (ret)
		goto error_unload_fw;

	mutex_unlock(&core->lock);

	return iris_wait_for_system_response(core);

error_unload_fw:
	iris_fw_unload(core);
error_power_off:
	iris_vpu_power_off(core);
error_queue_deinit:
	iris_hfi_queues_deinit(core);
error:
	core->state = IRIS_CORE_DEINIT;
exit:
	mutex_unlock(&core->lock);

	return ret;
}
