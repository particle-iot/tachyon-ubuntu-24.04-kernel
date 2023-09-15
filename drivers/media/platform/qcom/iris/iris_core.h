/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_CORE_H_
#define _IRIS_CORE_H_

#include <linux/types.h>
#include <media/v4l2-device.h>

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
};

#endif
