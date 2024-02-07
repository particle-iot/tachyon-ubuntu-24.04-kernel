/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_IFE_CSID_LITE_692_H_
#define _CAM_IFE_CSID_LITE_692_H_

#include "cam_ife_csid_common.h"
#include "cam_ife_csid_dev.h"
#include "cam_ife_csid_hw_ver2.h"
#include "cam_irq_controller.h"
#include "cam_isp_hw_mgr_intf.h"

static struct cam_ife_csid_ver2_reg_info cam_ife_csid_lite_692_reg_info = {
	.top_irq_reg_info      = &cam_ife_csid_lite_690_top_irq_reg_info,
	.rx_irq_reg_info       = &cam_ife_csid_lite_690_rx_irq_reg_info,
	.path_irq_reg_info     = {
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_0],
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_1],
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_2],
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_3],
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_4],
		&cam_ife_csid_lite_690_path_irq_reg_info[CAM_IFE_PIX_PATH_RES_RDI_5],
		},
	.buf_done_irq_reg_info = &cam_ife_csid_lite_690_buf_done_irq_reg_info,
	.cmn_reg                              = &cam_ife_csid_lite_690_cmn_reg_info,
	.csi2_reg                             = &cam_ife_csid_lite_690_csi2_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_IPP]   = NULL,
	.path_reg[CAM_IFE_PIX_PATH_RES_PPP]   = NULL,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_0] = &cam_ife_csid_lite_690_rdi_0_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_1] = &cam_ife_csid_lite_690_rdi_1_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_2] = &cam_ife_csid_lite_690_rdi_2_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_3] = &cam_ife_csid_lite_690_rdi_3_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_4] = &cam_ife_csid_lite_690_rdi_4_reg_info,
	.path_reg[CAM_IFE_PIX_PATH_RES_RDI_5] = &cam_ife_csid_lite_690_rdi_5_reg_info,
	.need_top_cfg = 0,
	.rx_irq_desc        = &cam_ife_csid_lite_690_rx_irq_desc,
	.path_irq_desc      = cam_ife_csid_lite_690_path_irq_desc,
	.num_top_err_irqs   = cam_ife_csid_690_lite_num_top_regs,
	.num_top_regs       = 1,
	.num_rx_regs        = 1,
};
#endif /* _CAM_IFE_CSID_LITE_692_H_ */
