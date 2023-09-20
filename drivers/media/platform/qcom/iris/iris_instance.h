/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_INSTANCE_H_
#define _IRIS_INSTANCE_H_

#include <media/v4l2-ctrls.h>

#include "iris_core.h"
#include "iris_common.h"

/**
 * struct iris_inst - holds per video instance parameters
 *
 * @list: used for attach an instance to the core
 * @core: pointer to core structure
 * @session_id: id of current video session
 * @vb2q_src: source vb2 queue
 * @vb2q_dst: destination vb2 queue
 * @ctx_q_lock: lock to serialize queues related ioctls
 * @fh: reference of v4l2 file handler
 * @fmt_src: structure of v4l2_format for source
 * @fmt_dst: structure of v4l2_format for destination
 * @ctrl_handler: reference of v4l2 ctrl handler
 * @packet: HFI packet
 * @packet_size: HFI packet size
 * @completions: structure of signal completions
 */

struct iris_inst {
	struct list_head		list;
	struct iris_core		*core;
	u32				session_id;
	struct vb2_queue		*vb2q_src;
	struct vb2_queue		*vb2q_dst;
	struct mutex			ctx_q_lock;/* lock to serialize queues related ioctls */
	struct v4l2_fh			fh;
	struct v4l2_format		*fmt_src;
	struct v4l2_format		*fmt_dst;
	struct v4l2_ctrl_handler	ctrl_handler;
	void				*packet;
	u32				packet_size;
	struct completion		completions[MAX_SIGNAL];
};

#endif
