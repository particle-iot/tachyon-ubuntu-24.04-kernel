/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_SFE_LITE_692_H_
#define _CAM_SFE_LITE_692_H_
#include "cam_sfe_core.h"
#include "cam_sfe_bus.h"
#include "cam_sfe_bus_rd.h"


struct cam_sfe_hw_info cam_sfe_lite_692_hw_info = {
	.irq_reg_info                  = &sfe_lite_690_top_irq_reg_info,

	/*SFE lite does not have support of bus write engine*/
	.bus_wr_version                = 0x0,

	.bus_rd_version                = CAM_SFE_BUS_RD_VER_1_0,
	.bus_rd_hw_info                = &sfe_lite_690_bus_rd_hw_info,

	.top_version                   = CAM_SFE_TOP_VER_1_0,
	.top_hw_info                   = &sfe_lite_690_top_hw_info,
};

#endif /* _CAM_SFE_LITE_692_H_ */
