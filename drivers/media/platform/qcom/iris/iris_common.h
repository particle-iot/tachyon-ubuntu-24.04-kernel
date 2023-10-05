/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _IRIS_COMMON_H_
#define _IRIS_COMMON_H_

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

struct iris_inst;

#define INPUT_MPLANE V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
#define OUTPUT_MPLANE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240
#define DEFAULT_BSE_VPP_DELAY    2

#define MAX_EVENTS   30

#define MB_IN_PIXEL (16 * 16)

#define NUM_MBS_4k (((4096 + 15) >> 4) * ((2304 + 15) >> 4))

enum codec_type {
	H264	= BIT(0),
	HEVC	= BIT(1),
	VP9	= BIT(2),
};

enum colorformat_type {
	FMT_NONE	= 0,
	FMT_NV12C	= BIT(0),
	FMT_NV12	= BIT(1),
	FMT_NV21	= BIT(2),
	FMT_TP10C	= BIT(3),
};

struct rect_desc {
	u32 left;
	u32 top;
	u32 width;
	u32 height;
};

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
