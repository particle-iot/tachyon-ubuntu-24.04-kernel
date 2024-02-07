/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_SFE_LITE_690_H_
#define _CAM_SFE_LITE_690_H_
#include "cam_sfe_core.h"
#include "cam_sfe_bus.h"
#include "cam_sfe_bus_rd.h"


static struct cam_sfe_top_debug_info sfe_lite_690_clc_dbg_module_info[CAM_SFE_TOP_DBG_REG_MAX][8] = {
	SFE_DBG_INFO_ARRAY_4bit(
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved",
		"test_bus_reserved"
	),
	SFE_DBG_INFO_ARRAY_4bit(
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle",
		"SFE_LITE vld/rdy/idle"
	),
};

static struct cam_sfe_top_common_reg_offset  sfe_lite_690_top_commong_reg  = {
	.hw_version                    = 0x00000000,
	.core_cgc_ctrl                 = 0x00000004,
	.ahb_clk_ovd                   = 0x00000008,
	.top_debug_cfg                 = 0x00000028,
	.lcr_supported                 = false,
	.ir_supported                  = false,
	.qcfa_only                     = false,
	.num_debug_registers           = 2,
	.top_cc_test_bus_supported     = false,
	.top_debug = {
		0x00000020,
		0x00000024,
	},
};

static struct cam_sfe_path_common_reg_data sfe_690_rd_top_reg_data = {
	.sof_irq_mask                  = 0x1,
	.eof_irq_mask                  = 0x2,
	.subscribe_irq_mask            = 0x6,
};


static struct cam_sfe_top_common_reg_data sfe_lite_690_top_common_reg_data = {
	.top_debug_cfg_en              = 0x1,
};

static struct cam_sfe_top_hw_info sfe_lite_690_top_hw_info = {
	.common_reg = &sfe_lite_690_top_commong_reg,
	.common_reg_data = &sfe_lite_690_top_common_reg_data,
	.rd_top_reg_data = &sfe_690_rd_top_reg_data,
	.num_inputs = 1,
	.input_type = {
		CAM_SFE_RD_VER_1_0,
	},
	.num_clc_module  = 2,
	.clc_dbg_mod_info = &sfe_lite_690_clc_dbg_module_info,
};

static struct cam_irq_register_set sfe_lite_690_bus_rd_irq_reg[1] = {
	{
		.mask_reg_offset   = 0x00000104,
		.clear_reg_offset  = 0x00000108,
		.status_reg_offset = 0x00000110,
		.set_reg_offset    = 0x00000118,
	},
};

static struct cam_sfe_bus_rd_constraint_error_desc
	sfe_lite_690_bus_rd_cons_error_desc[CAM_SFE_BUS_RD_CONS_ERR_MAX] = {
	{
		.bitmask    = BIT(0),
		.error_desc = "Image Addr Unalign Latch",
	},
	{
		.bitmask    = BIT(1),
		.error_desc = "Ubwc Addr Unalign Latch",
	},
	{
		.bitmask    = BIT(2),
		.error_desc = "Stride Unalign Latch",
	},
	{
		.bitmask    = BIT(3),
		.error_desc = "Y Unit Unalign Latch",
	},
	{
		.bitmask    = BIT(4),
		.error_desc = "X Unit Unalign Latch",
	},
	{
		.bitmask    = BIT(5),
		.error_desc = "Image width Unalign Latch",
	},
	{
		.bitmask    = BIT(6),
		.error_desc = "Image height Unalign Latch",
	},
	{
		.bitmask    = BIT(7),
		.error_desc = "Meta Stride Unalign Latch",
	},
};

static struct cam_sfe_bus_rd_constraint_error_info sfe_lite_690_bus_rd_constraint_error_info = {
	.constraint_error_list = sfe_lite_690_bus_rd_cons_error_desc,
	.num_cons_err          = 8,
};

static struct cam_sfe_bus_rd_hw_info sfe_lite_690_bus_rd_hw_info = {
	.common_reg = {
		.hw_version                   = 0x00000500,
		.misr_reset                   = 0x0000051C,
		.pwr_iso_cfg                  = 0x00000524,
		.input_if_cmd                 = 0x00000514,
		.test_bus_ctrl                = 0x0000052C,
		.security_cfg                 = 0x00000520,
		.cons_violation_status        = 0x00000534,
		.irq_reg_info = {
			.num_registers = 1,
			.irq_reg_set = sfe_lite_690_bus_rd_irq_reg,
			.global_irq_cmd_offset = 0x0000050C,
			.global_clear_bitmask  = 0x00000001,
			.clear_all_bitmask     = 0xFFFFFFFF,
		},
	},
	.num_client = 1,
	.bus_client_reg = {
		/* BUS Client 0 */
		{
			.cfg                      = 0x00000550,
			.image_addr               = 0x00000558,
			.buf_width                = 0x0000055C,
			.buf_height               = 0x00000560,
			.stride                   = 0x00000564,
			.unpacker_cfg             = 0x00000568,
			.latency_buf_allocation   = 0x0000057C,
			.system_cache_cfg         = 0x0000059C,
			.debug_status_cfg         = 0x00000590,
			.debug_status_0           = 0x00000594,
			.debug_status_1           = 0x00000598,
			.name                     = "Fetch0",
		},
	},
	.num_bus_rd_resc = 1,
	.sfe_bus_rd_info = {
		{
			.sfe_bus_rd_type = CAM_SFE_BUS_RD_RDI0,
			.mid[0] = 0,
			.max_width     = -1,
			.max_height    = -1,
			.max_clk_threshold = 600000000,
			.nom_clk_threshold = 600000000,
			.min_clk_threshold = 480000000,
			.bytes_per_clk     = 2,
		},
	},
	.top_irq_shift = 0x0,
	/*
	 * Refer to CAMNOC HPG for the updated value for a given target
	 * 48 OTs, 2 SFEs each with 3 RDs, 48 / 6 = 8
	 * We can allocate 256 * 8 = 2048 bytes. 256 bytes being
	 * the minimum
	 */
	.latency_buf_allocation = 2048,
	.sys_cache_default_val  = 0x20,
	.irq_err_mask           = 0x9,
	.constraint_error_info  = &sfe_lite_690_bus_rd_constraint_error_info,
	.rd_only                = true,
};

static struct cam_irq_register_set sfe_lite_690_top_irq_reg_set[1] = {
	{
		.mask_reg_offset   = 0x00000010,
		.clear_reg_offset  = 0x00000014,
		.status_reg_offset = 0x00000018,
		.set_reg_offset    = 0x0000001C,
		.test_set_val      = BIT(0),
		.test_sub_val      = BIT(0),
	},
};

static struct cam_irq_controller_reg_info sfe_lite_690_top_irq_reg_info = {
	.num_registers = 1,
	.irq_reg_set = sfe_lite_690_top_irq_reg_set,
	.global_irq_cmd_offset = 0x0000000C,
	.global_clear_bitmask  = 0x00000001,
	.global_set_bitmask    = 0x00000010,
	.clear_all_bitmask     = 0xFFFFFFFF,
};

struct cam_sfe_hw_info cam_sfe_lite_690_hw_info = {
	.irq_reg_info                  = &sfe_lite_690_top_irq_reg_info,

	/*SFE lite does not have support of bus write engine*/
	.bus_wr_version                = 0x0,

	.bus_rd_version                = CAM_SFE_BUS_RD_VER_1_0,
	.bus_rd_hw_info                = &sfe_lite_690_bus_rd_hw_info,

	.top_version                   = CAM_SFE_TOP_VER_1_0,
	.top_hw_info                   = &sfe_lite_690_top_hw_info,
};

#endif /* _CAM_SFE_LITE_690_H_ */
