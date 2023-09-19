// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_state.h"

static int iris_core_deinit_locked(struct iris_core *core)
{
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (core->state == IRIS_CORE_DEINIT)
		return 0;

	iris_hfi_core_deinit(core);

	iris_change_core_state(core, IRIS_CORE_DEINIT);

	return ret;
}

int iris_core_deinit(struct iris_core *core)
{
	int ret;

	mutex_lock(&core->lock);
	ret = iris_core_deinit_locked(core);
	mutex_unlock(&core->lock);

	return ret;
}

int iris_core_init(struct iris_core *core)
{
	int ret = 0;

	mutex_lock(&core->lock);
	if (core_in_valid_state(core)) {
		goto unlock;
	} else if (core->state == IRIS_CORE_ERROR) {
		ret = -EINVAL;
		goto unlock;
	}

	if (iris_change_core_state(core, IRIS_CORE_INIT_WAIT))
		iris_change_core_state(core, IRIS_CORE_ERROR);

	ret = iris_hfi_core_init(core);
	if (ret) {
		iris_change_core_state(core, IRIS_CORE_ERROR);
		dev_err(core->dev, "%s: core init failed\n", __func__);
		iris_core_deinit_locked(core);
		goto unlock;
	}

unlock:
	mutex_unlock(&core->lock);

	return ret;
}
