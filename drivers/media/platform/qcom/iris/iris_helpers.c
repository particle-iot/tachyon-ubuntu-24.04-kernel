// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_instance.h"

int check_core_lock(struct iris_core *core)
{
	bool fatal = !mutex_is_locked(&core->lock);

	WARN_ON(fatal);

	return fatal ? -EINVAL : 0;
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

static int process_inst_timeout(struct iris_inst *inst)
{
	struct iris_inst *instance;
	struct iris_core *core;
	bool found = false;
	int ret = 0;

	core = inst->core;

	mutex_lock(&core->lock);
	list_for_each_entry(instance, &core->instances, list) {
		if (instance == inst) {
			found = true;
			break;
		}
	}
	if (!found) {
		ret = -EINVAL;
		goto unlock;
	}

	iris_change_core_state(core, IRIS_CORE_ERROR);

	iris_core_deinit_locked(core);

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int close_session(struct iris_inst *inst)
{
	u32 hw_response_timeout_val;
	bool wait_for_response;
	struct iris_core *core;
	int ret;

	core = inst->core;
	hw_response_timeout_val = core->cap[HW_RESPONSE_TIMEOUT].value;
	wait_for_response = true;
	ret = iris_hfi_session_close(inst);
	if (ret)
		wait_for_response = false;

	kfree(inst->packet);
	inst->packet = NULL;

	if (wait_for_response) {
		ret = wait_for_completion_timeout(&inst->completions[SIGNAL_CMD_CLOSE],
						  msecs_to_jiffies(hw_response_timeout_val));
		if (!ret) {
			ret = -ETIMEDOUT;
			process_inst_timeout(inst);
		}
	}

	return ret;
}
