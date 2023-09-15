/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_STATE_H_
#define _IRIS_STATE_H_

struct iris_core;

enum iris_core_state {
	IRIS_CORE_DEINIT,
	IRIS_CORE_INIT_WAIT,
	IRIS_CORE_INIT,
	IRIS_CORE_ERROR,
};

bool core_in_valid_state(struct iris_core *core);
int iris_change_core_state(struct iris_core *core,
			   enum iris_core_state request_state);

#endif
