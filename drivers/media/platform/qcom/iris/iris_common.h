/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _IRIS_COMMON_H_
#define _IRIS_COMMON_H_

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

struct iris_inst;

#define INPUT_MPLANE V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define OUTPUT_MPLANE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_BUF_SIZE 115200

enum signal_session_response {
	SIGNAL_CMD_STOP_INPUT = 0,
	SIGNAL_CMD_STOP_OUTPUT,
	SIGNAL_CMD_CLOSE,
	MAX_SIGNAL,
};

enum iris_buffer_type {
	BUF_NONE,
	BUF_INPUT,
	BUF_OUTPUT,
	BUF_READ_ONLY,
	BUF_BIN,
	BUF_ARP,
	BUF_COMV,
	BUF_NON_COMV,
	BUF_LINE,
	BUF_DPB,
	BUF_PERSIST,
	BUF_VPSS,
};

enum iris_buffer_attributes {
	BUF_ATTR_DEFERRED		= BIT(0),
	BUF_ATTR_READ_ONLY		= BIT(1),
	BUF_ATTR_PENDING_RELEASE	= BIT(2),
	BUF_ATTR_QUEUED			= BIT(3),
	BUF_ATTR_DEQUEUED		= BIT(4),
	BUF_ATTR_BUFFER_DONE		= BIT(5),
};

struct iris_buffer {
	struct list_head		list;
	struct iris_inst		*inst;
	enum iris_buffer_type		type;
	u32				index;
	int				fd;
	u32				buffer_size;
	u32				data_offset;
	u32				data_size;
	u64				device_addr;
	void				*kvaddr;
	unsigned long			dma_attrs;
	u32				flags;
	u64				timestamp;
	enum iris_buffer_attributes	attr;
	void				*dmabuf;
	struct sg_table			*sg_table;
	struct dma_buf_attachment	*attach;
};

#endif
