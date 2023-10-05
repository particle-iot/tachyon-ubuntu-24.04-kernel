// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <dt-bindings/clock/qcom,sm8550-gcc.h>
#include <dt-bindings/clock/qcom,sm8450-videocc.h>

#include <media/v4l2-ctrls.h>

#include "hfi_defines.h"
#include "iris_ctrls.h"
#include "platform_common.h"
#include "resources.h"

#define CODECS_ALL     (H264 | HEVC | VP9)

#define DEFAULT_FPS        30
#define MINIMUM_FPS         1
#define MAXIMUM_FPS       480

static struct codec_info codec_data_sm8550[] = {
	{
		.v4l2_codec  = V4L2_PIX_FMT_H264,
		.codec  = H264,
	},
	{
		.v4l2_codec  = V4L2_PIX_FMT_HEVC,
		.codec  = HEVC,
	},
	{
		.v4l2_codec  = V4L2_PIX_FMT_VP9,
		.codec  = VP9,
	},
};

static struct color_format_info color_format_data_sm8550[] = {
	{
		.v4l2_color_format = V4L2_PIX_FMT_NV12,
		.color_format = FMT_NV12,
	},
	{
		.v4l2_color_format = V4L2_PIX_FMT_NV21,
		.color_format = FMT_NV21,
	},
	{
		.v4l2_color_format = V4L2_PIX_FMT_QC08C,
		.color_format = FMT_NV12C,
	},
	{
		.v4l2_color_format = V4L2_PIX_FMT_QC10C,
		.color_format = FMT_TP10C,
	},
};

static struct plat_core_cap core_data_sm8550[] = {
	{DEC_CODECS, H264 | HEVC | VP9},
	{MAX_SESSION_COUNT, 16},
	{MAX_MBPF, 278528}, /* ((8192x4352)/256) * 2 */
	{MAX_MBPS, 7833600}, /* max_load 7680x4320@60fps */
	{NUM_VPP_PIPE, 4},
	{HW_RESPONSE_TIMEOUT, HW_RESPONSE_TIMEOUT_VALUE},
	{DMA_MASK, GENMASK(31, 29) - 1},
	{CP_START, 0},
	{CP_SIZE, 0x25800000},
	{CP_NONPIXEL_START, 0x01000000},
	{CP_NONPIXEL_SIZE, 0x24800000},
};

static struct plat_inst_cap instance_cap_data_sm8550[] = {
	{FRAME_WIDTH, CODECS_ALL, 96, 8192, 1, 1920},

	{FRAME_WIDTH, VP9, 96, 4096, 1, 1920},

	{FRAME_HEIGHT, CODECS_ALL, 96, 8192, 1, 1080},

	{FRAME_HEIGHT, VP9, 96, 4096, 1, 1080},

	{PIX_FMTS, H264,
		FMT_NV12,
		FMT_NV12C,
		FMT_NV12 | FMT_NV21 | FMT_NV12C,
		FMT_NV12C},

	{PIX_FMTS, HEVC,
		FMT_NV12,
		FMT_TP10C,
		FMT_NV12 | FMT_NV21 | FMT_NV12C | FMT_TP10C,
		FMT_NV12C,
		0, 0,
		CAP_FLAG_NONE,
		{PROFILE}},

	{PIX_FMTS, VP9,
		FMT_NV12,
		FMT_TP10C,
		FMT_NV12 | FMT_NV21 | FMT_NV12C | FMT_TP10C,
		FMT_NV12C},

	{MBPF, CODECS_ALL, 36, 138240, 1, 138240},

	/* (4096 * 2304) / 256 */
	{MBPF, VP9, 36, 36864, 1, 36864},

	{QUEUED_RATE, CODECS_ALL,
		(MINIMUM_FPS << 16), INT_MAX,
		1, (DEFAULT_FPS << 16)},

	{MB_CYCLES_VSP, CODECS_ALL, 25, 25, 1, 25},

	{MB_CYCLES_VSP, VP9, 60, 60, 1, 60},

	{MB_CYCLES_VPP, CODECS_ALL, 200, 200, 1, 200},

	{MB_CYCLES_LP, CODECS_ALL, 200, 200, 1, 200},

	{MB_CYCLES_FW, CODECS_ALL, 489583, 489583, 1, 489583},

	{MB_CYCLES_FW_VPP, CODECS_ALL, 66234, 66234, 1, 66234},

	{NUM_COMV, CODECS_ALL,
		0, INT_MAX, 1, 0},

	{PROFILE, H264,
		V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH,
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH) |
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
		BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH),
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		HFI_PROP_PROFILE,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{PROFILE, HEVC,
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
		BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE) |
		BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10),
		V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		HFI_PROP_PROFILE,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		adjust_profile,
		set_u32_enum},

	{PROFILE, VP9,
		V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		V4L2_MPEG_VIDEO_VP9_PROFILE_2,
		BIT(V4L2_MPEG_VIDEO_VP9_PROFILE_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_PROFILE_2),
		V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		V4L2_CID_MPEG_VIDEO_VP9_PROFILE,
		HFI_PROP_PROFILE,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{LEVEL, H264,
		V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		V4L2_MPEG_VIDEO_H264_LEVEL_6_2,
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1B) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_2) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_3) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_2_2) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_3_2) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_4_2) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_2) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_0) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_1) |
		BIT(V4L2_MPEG_VIDEO_H264_LEVEL_6_2),
		V4L2_MPEG_VIDEO_H264_LEVEL_6_1,
		V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		HFI_PROP_LEVEL,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{LEVEL, HEVC,
		V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1) |
		BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2),
		V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1,
		V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		HFI_PROP_LEVEL,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{LEVEL, VP9,
		V4L2_MPEG_VIDEO_VP9_LEVEL_1_0,
		V4L2_MPEG_VIDEO_VP9_LEVEL_6_0,
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_1_1) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_2_1) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_3_1) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_4_1) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_0) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_1) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_5_2) |
		BIT(V4L2_MPEG_VIDEO_VP9_LEVEL_6_0),
		V4L2_MPEG_VIDEO_VP9_LEVEL_6_0,
		V4L2_CID_MPEG_VIDEO_VP9_LEVEL,
		HFI_PROP_LEVEL,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{HEVC_TIER, HEVC,
		V4L2_MPEG_VIDEO_HEVC_TIER_MAIN,
		V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		BIT(V4L2_MPEG_VIDEO_HEVC_TIER_MAIN) |
		BIT(V4L2_MPEG_VIDEO_HEVC_TIER_HIGH),
		V4L2_MPEG_VIDEO_HEVC_TIER_HIGH,
		V4L2_CID_MPEG_VIDEO_HEVC_TIER,
		HFI_PROP_TIER,
		CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		{0},
		NULL,
		set_u32_enum},

	{DISPLAY_DELAY_ENABLE, CODECS_ALL,
		0, 1, 1, 0,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE,
		HFI_PROP_DECODE_ORDER_OUTPUT,
		CAP_FLAG_INPUT_PORT,
		{OUTPUT_ORDER},
		NULL,
		NULL},

	{DISPLAY_DELAY, CODECS_ALL,
		0, 1, 1, 0,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY,
		HFI_PROP_DECODE_ORDER_OUTPUT,
		CAP_FLAG_INPUT_PORT,
		{OUTPUT_ORDER},
		NULL,
		NULL},

	{OUTPUT_ORDER, CODECS_ALL,
		0, 1, 1, 0,
		0,
		HFI_PROP_DECODE_ORDER_OUTPUT,
		CAP_FLAG_INPUT_PORT,
		{0},
		adjust_output_order,
		set_u32},

	{STAGE, CODECS_ALL,
		STAGE_1,
		STAGE_2, 1,
		STAGE_2,
		0,
		HFI_PROP_STAGE,
		CAP_FLAG_NONE,
		{0},
		NULL,
		set_stage},

	{PIPE, CODECS_ALL,
		PIPE_1,
		PIPE_4, 1,
		PIPE_4,
		0,
		HFI_PROP_PIPE,
		CAP_FLAG_NONE,
		{0},
		NULL,
		set_pipe},

	{POC, H264, 0, 2, 1, 1,
		0,
		HFI_PROP_PIC_ORDER_CNT_TYPE},

	{CODED_FRAMES, H264 | HEVC,
		CODED_FRAMES_PROGRESSIVE, CODED_FRAMES_PROGRESSIVE,
		0, CODED_FRAMES_PROGRESSIVE,
		0,
		HFI_PROP_CODED_FRAMES},

	{BIT_DEPTH, CODECS_ALL, BIT_DEPTH_8, BIT_DEPTH_10, 1, BIT_DEPTH_8,
		0,
		HFI_PROP_LUMA_CHROMA_BIT_DEPTH},

	{DEFAULT_HEADER, CODECS_ALL,
		0, 1, 1, 0,
		0,
		HFI_PROP_DEC_DEFAULT_HEADER},

	{RAP_FRAME, CODECS_ALL,
		0, 1, 1, 1,
		0,
		HFI_PROP_DEC_START_FROM_RAP_FRAME,
		CAP_FLAG_INPUT_PORT,
		{0},
		NULL,
		set_u32},
};

static const struct bus_info sm8550_bus_table[] = {
	{ NULL, "iris-cnoc", 1000, 1000     },
	{ NULL, "iris-ddr",  1000, 15000000 },
};

static const struct clock_info sm8550_clk_table[] = {
	{ NULL, "gcc_video_axi0", GCC_VIDEO_AXI0_CLK, 0 },
	{ NULL, "core_clk",       VIDEO_CC_MVS0C_CLK, 0 },
	{ NULL, "vcodec_core",    VIDEO_CC_MVS0_CLK,  1 },
};

static const char * const sm8550_clk_reset_table[] = { "video_axi_reset", NULL };

static const char * const sm8550_pd_table[] = { "iris-ctl", "vcodec", NULL };

static const char * const sm8550_opp_pd_table[] = { "mxc", "mmcx", NULL };

static const struct bw_info sm8550_bw_table_dec[] = {
	{ 2073600, 1608000, 2742000 },	/* 4096x2160@60 */
	{ 1036800,  826000, 1393000 },	/* 4096x2160@30 */
	{  489600,  567000,  723000 },	/* 1920x1080@60 */
	{  244800,  294000,  372000 },	/* 1920x1080@30 */
};

static const struct reg_preset_info sm8550_reg_preset_table[] = {
	{ 0xB0088, 0x0, 0x11 },
};

static struct ubwc_config_data ubwc_config_sm8550[] = {
	UBWC_CONFIG(8, 32, 16, 0, 1, 1, 1),
};

static struct format_capability format_data_sm8550 = {
	.codec_info = codec_data_sm8550,
	.codec_info_size = ARRAY_SIZE(codec_data_sm8550),
	.color_format_info = color_format_data_sm8550,
	.color_format_info_size = ARRAY_SIZE(color_format_data_sm8550),
};

struct platform_data sm8550_data = {
	.bus_tbl = sm8550_bus_table,
	.bus_tbl_size = ARRAY_SIZE(sm8550_bus_table),
	.clk_tbl = sm8550_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8550_clk_table),
	.clk_rst_tbl = sm8550_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8550_clk_reset_table),

	.bw_tbl_dec = sm8550_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8550_bw_table_dec),

	.pd_tbl = sm8550_pd_table,
	.pd_tbl_size = ARRAY_SIZE(sm8550_pd_table),
	.opp_pd_tbl = sm8550_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8550_opp_pd_table),

	.reg_prst_tbl = sm8550_reg_preset_table,
	.reg_prst_tbl_size = ARRAY_SIZE(sm8550_reg_preset_table),
	.fwname = "vpu30_4v",
	.pas_id = 9,

	.core_data = core_data_sm8550,
	.core_data_size = ARRAY_SIZE(core_data_sm8550),
	.inst_cap_data = instance_cap_data_sm8550,
	.inst_cap_data_size = ARRAY_SIZE(instance_cap_data_sm8550),
	.ubwc_config = ubwc_config_sm8550,
	.format_data = &format_data_sm8550,
};
