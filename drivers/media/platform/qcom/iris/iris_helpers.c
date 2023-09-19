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
