/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _PLATFORM_COMMON_H_
#define _PLATFORM_COMMON_H_

#include <linux/bits.h>
#include <media/v4l2-ctrls.h>

#include "iris_common.h"

struct iris_core;
struct iris_inst;

#define HW_RESPONSE_TIMEOUT_VALUE     (1000)
#define AUTOSUSPEND_DELAY_VALUE       (HW_RESPONSE_TIMEOUT_VALUE + 500)

#define BIT_DEPTH_8 (8 << 16 | 8)
#define BIT_DEPTH_10 (10 << 16 | 10)

#define CODED_FRAMES_PROGRESSIVE 0x0
#define CODED_FRAMES_INTERLACE 0x1
#define MAX_NUM_CHILD         10

#define UBWC_CONFIG(mc, ml, hbb, bs1, bs2, bs3, bsp) \
{	                                                 \
	.max_channels = mc,                              \
	.mal_length = ml,                                \
	.highest_bank_bit = hbb,                         \
	.bank_swzl_level = bs1,                          \
	.bank_swz2_level = bs2,                          \
	.bank_swz3_level = bs3,                          \
	.bank_spreading = bsp,                           \
}

enum stage_type {
	STAGE_NONE = 0,
	STAGE_1 = 1,
	STAGE_2 = 2,
};

enum pipe_type {
	PIPE_NONE = 0,
	PIPE_1 = 1,
	PIPE_2 = 2,
	PIPE_4 = 4,
};

extern struct platform_data sm8550_data;

struct bw_info {
	u32 mbs_per_sec;
	u32 bw_ddr;
	u32 bw_ddr_10bit;
};

struct reg_preset_info {
	u32              reg;
	u32              value;
	u32              mask;
};

struct iris_core_power {
	u64 clk_freq;
	u64 bus_bw;
};

struct ubwc_config_data {
	u32	max_channels;
	u32	mal_length;
	u32	highest_bank_bit;
	u32	bank_swzl_level;
	u32	bank_swz2_level;
	u32	bank_swz3_level;
	u32	bank_spreading;
};

struct bus_vote_data {
	u32 color_formats[2];
	int height, width;
	u32 fps;
	u64 bus_bw;
};

struct iris_inst_power {
	u64 min_freq;
	u32 bus_bw;
};

enum plat_core_cap_type {
	CORE_CAP_NONE = 0,
	DEC_CODECS,
	MAX_SESSION_COUNT,
	MAX_MBPF,
	MAX_MBPS,
	MAX_MBPF_HQ,
	MAX_MBPS_HQ,
	MAX_MBPF_B_FRAME,
	MAX_MBPS_B_FRAME,
	MAX_ENH_LAYER_COUNT,
	NUM_VPP_PIPE,
	FW_UNLOAD,
	FW_UNLOAD_DELAY,
	HW_RESPONSE_TIMEOUT,
	NON_FATAL_FAULTS,
	DMA_MASK,
	CP_START,
	CP_SIZE,
	CP_NONPIXEL_START,
	CP_NONPIXEL_SIZE,
	CORE_CAP_MAX,
};

struct plat_core_cap {
	enum plat_core_cap_type type;
	u32 value;
};

enum plat_inst_cap_type {
	INST_CAP_NONE = 0,
	FRAME_WIDTH,
	FRAME_HEIGHT,
	PIX_FMTS,
	MBPF,
	QUEUED_RATE,
	MB_CYCLES_VSP,
	MB_CYCLES_VPP,
	MB_CYCLES_LP,
	MB_CYCLES_FW,
	MB_CYCLES_FW_VPP,
	NUM_COMV,
	ENTROPY_MODE,
	PROFILE,
	LEVEL,
	HEVC_TIER,
	DISPLAY_DELAY_ENABLE,
	DISPLAY_DELAY,
	OUTPUT_ORDER,
	STAGE,
	PIPE,
	POC,
	CODED_FRAMES,
	BIT_DEPTH,
	DEFAULT_HEADER,
	RAP_FRAME,
	INST_CAP_MAX,
};

enum plat_inst_cap_flags {
	CAP_FLAG_NONE			= 0,
	CAP_FLAG_DYNAMIC_ALLOWED	= BIT(0),
	CAP_FLAG_MENU			= BIT(1),
	CAP_FLAG_INPUT_PORT		= BIT(2),
	CAP_FLAG_OUTPUT_PORT		= BIT(3),
	CAP_FLAG_CLIENT_SET		= BIT(4),
	CAP_FLAG_BITMASK		= BIT(5),
	CAP_FLAG_VOLATILE		= BIT(6),
};

struct plat_inst_cap {
	enum plat_inst_cap_type cap_id;
	enum codec_type codec;
	s32 min;
	s32 max;
	u32 step_or_mask;
	s32 value;
	u32 v4l2_id;
	u32 hfi_id;
	enum plat_inst_cap_flags flags;
	enum plat_inst_cap_type children[MAX_NUM_CHILD];
	int (*adjust)(struct iris_inst *inst,
		      struct v4l2_ctrl *ctrl);
	int (*set)(struct iris_inst *inst,
		   enum plat_inst_cap_type cap_id);
};

struct plat_inst_caps {
	enum codec_type codec;
	struct plat_inst_cap cap[INST_CAP_MAX + 1];
};

struct codec_info {
	u32 v4l2_codec;
	enum codec_type codec;
};

struct color_format_info {
	u32 v4l2_color_format;
	enum colorformat_type color_format;
};

struct format_capability {
	struct codec_info *codec_info;
	u32 codec_info_size;
	struct color_format_info *color_format_info;
	u32 color_format_info_size;
};

struct platform_data {
	const struct bus_info *bus_tbl;
	unsigned int bus_tbl_size;
	const struct bw_info *bw_tbl_dec;
	unsigned int bw_tbl_dec_size;
	const char * const *pd_tbl;
	unsigned int pd_tbl_size;
	const char * const *opp_pd_tbl;
	unsigned int opp_pd_tbl_size;
	const struct clock_info *clk_tbl;
	unsigned int clk_tbl_size;
	const char * const *clk_rst_tbl;
	unsigned int clk_rst_tbl_size;
	const struct reg_preset_info *reg_prst_tbl;
	unsigned int reg_prst_tbl_size;
	struct ubwc_config_data *ubwc_config;
	struct format_capability *format_data;
	const char *fwname;
	u32 pas_id;
	struct plat_core_cap *core_data;
	u32 core_data_size;
	struct plat_inst_cap *inst_cap_data;
	u32 inst_cap_data_size;
	const u32 *avc_subscribe_param;
	unsigned int avc_subscribe_param_size;
	const u32 *hevc_subscribe_param;
	unsigned int hevc_subscribe_param_size;
	const u32 *vp9_subscribe_param;
	unsigned int vp9_subscribe_param_size;
	const u32 *dec_input_prop;
	unsigned int dec_input_prop_size;
	const u32 *dec_output_prop_avc;
	unsigned int dec_output_prop_size_avc;
	const u32 *dec_output_prop_hevc;
	unsigned int dec_output_prop_size_hevc;
	const u32 *dec_output_prop_vp9;
	unsigned int dec_output_prop_size_vp9;
};

int init_platform(struct iris_core *core);

#endif
