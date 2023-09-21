// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"

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
