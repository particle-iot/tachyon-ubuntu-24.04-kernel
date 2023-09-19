/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CORE_H_
#define _IRIS_CORE_H_

#include <linux/types.h>
#include <media/v4l2-device.h>

#include "iris_hfi_queue.h"
#include "iris_state.h"
#include "resources.h"

/**
 * struct iris_core - holds core parameters valid for all instances
 *
 * @dev: reference to device structure
 * @reg_base: IO memory base address
 * @irq: iris irq
 * @v4l2_dev: a holder for v4l2 device structure
 * @vdev_dec: iris video device structure for decoder
 * @v4l2_file_ops: iris v4l2 file ops
 * @v4l2_ioctl_ops: iris v4l2 ioctl ops
 * @bus_tbl: table of iris buses
 * @bus_count: count of iris buses
 * @power_domain_tbl: table of iris power domains
 * @pd_count: count of iris power domains
 * @clock_tbl: table of iris clocks
 * @clk_count: count of iris clocks
 * @reset_tbl: table of iris reset clocks
 * @reset_count: count of iris reset clocks
 * @state: current state of core
 * @iface_q_table: Interface queue table memory
 * @command_queue: shared interface queue to send commands to firmware
 * @message_queue: shared interface queue to receive responses from firmware
 * @debug_queue: shared interface queue to receive debug info from firmware
 * @sfr: SFR register memory
 * @lock: a lock for this strucure
 * @packet: pointer to packet from driver to fw
 * @packet_size: size of packet
 * @sys_init_id: id of sys init packet
 * @header_id: id of packet header
 * @packet_id: id of packet
 */

struct iris_core {
	struct device				*dev;
	void __iomem				*reg_base;
	int					irq;
	struct v4l2_device			v4l2_dev;
	struct video_device			*vdev_dec;
	const struct v4l2_file_operations	*v4l2_file_ops;
	const struct v4l2_ioctl_ops		*v4l2_ioctl_ops;
	struct bus_info				*bus_tbl;
	u32					bus_count;
	struct power_domain_info		*power_domain_tbl;
	u32					pd_count;
	struct clock_info			*clock_tbl;
	u32					clk_count;
	struct reset_info			*reset_tbl;
	u32					reset_count;
	enum iris_core_state			state;
	struct mem_desc				iface_q_table;
	struct iface_q_info			command_queue;
	struct iface_q_info			message_queue;
	struct iface_q_info			debug_queue;
	struct mem_desc				sfr;
	struct mutex				lock; /* lock for core structure */
	u8					*packet;
	u32					packet_size;
	u32					sys_init_id;
	u32					header_id;
	u32					packet_id;
};

int iris_core_init(struct iris_core *core);
int iris_core_deinit(struct iris_core *core);

#endif
