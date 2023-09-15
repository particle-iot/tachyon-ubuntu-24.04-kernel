// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_state.h"

#define IRIS_STATE(name)[IRIS_CORE_##name] = "CORE_"#name

static const char * const core_state_names[] = {
	IRIS_STATE(DEINIT),
	IRIS_STATE(INIT_WAIT),
	IRIS_STATE(INIT),
	IRIS_STATE(ERROR),
};

#undef IRIS_STATE

bool core_in_valid_state(struct iris_core *core)
{
	return core->state == IRIS_CORE_INIT ||
		core->state == IRIS_CORE_INIT_WAIT;
}

static const char *core_state_name(enum iris_core_state state)
{
	if ((unsigned int)state < ARRAY_SIZE(core_state_names))
		return core_state_names[state];

	return "UNKNOWN_STATE";
}

static bool iris_allow_core_state_change(struct iris_core *core,
					 enum iris_core_state req_state)
{
	if (core->state == IRIS_CORE_DEINIT)
		return req_state == IRIS_CORE_INIT_WAIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_INIT_WAIT)
		return req_state == IRIS_CORE_INIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_INIT)
		return req_state == IRIS_CORE_DEINIT || req_state == IRIS_CORE_ERROR;
	else if (core->state == IRIS_CORE_ERROR)
		return req_state == IRIS_CORE_DEINIT;

	dev_warn(core->dev, "core state change %s -> %s is not allowed\n",
		 core_state_name(core->state), core_state_name(req_state));

	return false;
}

int iris_change_core_state(struct iris_core *core,
			   enum iris_core_state request_state)
{
	if (core->state == request_state)
		return 0;

	if (!iris_allow_core_state_change(core, request_state))
		return -EINVAL;

	core->state = request_state;

	return 0;
}
