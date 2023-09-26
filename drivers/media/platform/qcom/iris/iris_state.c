// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_helpers.h"
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
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	if (core->state == request_state)
		return 0;

	if (!iris_allow_core_state_change(core, request_state))
		return -EINVAL;

	core->state = request_state;

	return ret;
}

struct iris_inst_state_change_allow {
	enum iris_inst_state        from;
	enum iris_inst_state        to;
	enum state_change           allow;
};

static enum state_change iris_allow_inst_state_change(struct iris_inst *inst,
						      enum iris_inst_state req_state)
{
	enum state_change allow = STATE_CHANGE_ALLOW;
	int cnt;
	static struct iris_inst_state_change_allow state[] = {
		/* from, to, allow */
		{IRIS_INST_OPEN,             IRIS_INST_OPEN,               STATE_CHANGE_IGNORE    },
		{IRIS_INST_OPEN,             IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_STREAMING,          STATE_CHANGE_DISALLOW  },
		{IRIS_INST_OPEN,             IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_OPEN,             IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_OPEN,               STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_IGNORE    },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_DISALLOW  },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_STREAMING,          STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_INPUT_STREAMING,  IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_OPEN,               STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_DISALLOW  },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_IGNORE    },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_STREAMING,          STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_OUTPUT_STREAMING, IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_STREAMING,        IRIS_INST_OPEN,               STATE_CHANGE_DISALLOW  },
		{IRIS_INST_STREAMING,        IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_STREAMING,          STATE_CHANGE_IGNORE    },
		{IRIS_INST_STREAMING,        IRIS_INST_CLOSE,              STATE_CHANGE_ALLOW     },
		{IRIS_INST_STREAMING,        IRIS_INST_ERROR,              STATE_CHANGE_ALLOW     },

		{IRIS_INST_CLOSE,            IRIS_INST_OPEN,               STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_STREAMING,          STATE_CHANGE_DISALLOW  },
		{IRIS_INST_CLOSE,            IRIS_INST_CLOSE,              STATE_CHANGE_IGNORE    },
		{IRIS_INST_CLOSE,            IRIS_INST_ERROR,              STATE_CHANGE_IGNORE    },

		{IRIS_INST_ERROR,            IRIS_INST_OPEN,               STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_INPUT_STREAMING,    STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_OUTPUT_STREAMING,   STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_STREAMING,          STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_CLOSE,              STATE_CHANGE_IGNORE    },
		{IRIS_INST_ERROR,            IRIS_INST_ERROR,              STATE_CHANGE_IGNORE    },
	};

	for (cnt = 0; cnt < ARRAY_SIZE(state); cnt++) {
		if (state[cnt].from == inst->state && state[cnt].to == req_state) {
			allow = state[cnt].allow;
			break;
		}
	}

	return allow;
}

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state)
{
	enum state_change allow;

	if (IS_SESSION_ERROR(inst))
		return 0;

	if (inst->state == request_state)
		return 0;

	allow = iris_allow_inst_state_change(inst, request_state);
	if (allow != STATE_CHANGE_ALLOW)
		return (allow == STATE_CHANGE_DISALLOW ? -EINVAL : 0);

	inst->state = request_state;

	return 0;
}
