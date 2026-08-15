// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_ctrls.h"
#include "iris_platform_common.h"
#include "iris_resources.h"
#include "iris_hfi_gen1.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_defines.h"
#include "iris_hfi_gen1_defines.h"
#include "iris_vpu_buffer.h"
#include "iris_vpu_common.h"

#include "iris_platform_sc7280.h"

#define BITRATE_MIN		32000
#define BITRATE_MAX		160000000
#define BITRATE_PEAK_DEFAULT	(BITRATE_DEFAULT * 2)
#define BITRATE_STEP		100

static const struct platform_inst_fw_cap inst_fw_cap_sm8250_dec[] = {
	{
		.cap_id = PIPE,
		/* .max, .min and .value are set via platform data */
		.step_or_mask = 1,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_ROUTE,
		.set = iris_set_pipe,
	},
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.set = iris_set_stage,
	},
};

static const struct platform_inst_fw_cap inst_fw_cap_sm8250_enc[] = {
	{
		.cap_id = STAGE,
		.min = STAGE_1,
		.max = STAGE_2,
		.step_or_mask = 1,
		.value = STAGE_2,
		.hfi_id = HFI_PROPERTY_PARAM_WORK_MODE,
		.set = iris_set_stage,
	},
	{
		.cap_id = PROFILE_H264,
		.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.max = V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_STEREO_HIGH) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH),
		.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = PROFILE_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE) |
				BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10),
		.value = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = LEVEL_H264,
		.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.max = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_LEVEL_1_0) |
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
				BIT(V4L2_MPEG_VIDEO_H264_LEVEL_5_1),
		.value = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = LEVEL_HEVC,
		.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEVC_LEVEL_1) |
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
		.value = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.hfi_id = HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_profile_level_gen1,
	},
	{
		.cap_id = HEADER_MODE,
		.min = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.max = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) |
				BIT(V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME),
		.value = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_header_mode_gen1,
	},
	{
		.cap_id = BITRATE,
		.min = BITRATE_MIN,
		.max = BITRATE_MAX,
		.step_or_mask = BITRATE_STEP,
		.value = BITRATE_DEFAULT,
		.hfi_id = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_INPUT_PORT |
			CAP_FLAG_DYNAMIC_ALLOWED,
		.set = iris_set_bitrate,
	},
	{
		.cap_id = BITRATE_MODE,
		.min = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.max = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_VBR) |
				BIT(V4L2_MPEG_VIDEO_BITRATE_MODE_CBR),
		.value = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_RATE_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_bitrate_mode_gen1,
	},
	{
		.cap_id = FRAME_SKIP_MODE,
		.min = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.max = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED) |
				BIT(V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT),
		.value = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
	},
	{
		.cap_id = FRAME_RC_ENABLE,
		.min = 0,
		.max = 1,
		.step_or_mask = 1,
		.value = 1,
	},
	{
		.cap_id = GOP_SIZE,
		.min = 0,
		.max = (1 << 16) - 1,
		.step_or_mask = 1,
		.value = 30,
		.set = iris_set_u32
	},
	{
		.cap_id = ENTROPY_MODE,
		.min = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.max = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.step_or_mask = BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC) |
				BIT(V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC),
		.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_H264_ENTROPY_CONTROL,
		.flags = CAP_FLAG_OUTPUT_PORT | CAP_FLAG_MENU,
		.set = iris_set_entropy_mode_gen1,
	},
	{
		.cap_id = MIN_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MIN_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP_HEVC,
		.step_or_mask = 1,
		.value = MIN_QP_8BIT,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MAX_FRAME_QP_H264,
		.min = MIN_QP_8BIT,
		.max = MAX_QP,
		.step_or_mask = 1,
		.value = MAX_QP,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
	{
		.cap_id = MAX_FRAME_QP_HEVC,
		.min = MIN_QP_8BIT,
		.max = MAX_QP_HEVC,
		.step_or_mask = 1,
		.value = MAX_QP_HEVC,
		.hfi_id = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2,
		.flags = CAP_FLAG_OUTPUT_PORT,
		.set = iris_set_qp_range,
	},
};

static struct platform_inst_caps platform_inst_cap_sm8250 = {
	.min_frame_width = 128,
	.max_frame_width = 8192,
	.min_frame_height = 128,
	.max_frame_height = 8192,
	.max_mbpf = 138240,
	.mb_cycles_vsp = 25,
	.mb_cycles_vpp = 200,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

/*
 * SC7280 tops out at 4096x2176@60fps, not the 8K its VPU2 sibling SM8250
 * advertises. The numbers matter beyond a sanity check: max_frame_width and
 * max_frame_height are handed straight to ENUM_FRAMESIZES, and clients probe
 * the frame intervals of the largest reported size to learn the frame rate
 * range. Reporting 8192x8192 therefore caps the whole encoder at the 8 fps
 * that size would sustain.
 */
static struct platform_inst_caps platform_inst_cap_sc7280 = {
	.min_frame_width = 128,
	.max_frame_width = 4096,
	.min_frame_height = 128,
	.max_frame_height = 2176,
	.max_mbpf = 4096 * 2176 / 256,
	.mb_cycles_vsp = 25,
	.mb_cycles_vpp = 200,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

static void iris_set_sm8250_preset_registers(struct iris_core *core)
{
	writel(0x0, core->reg_base + 0xB0088);
}

static const struct icc_info sm8250_icc_table[] = {
	{ "cpu-cfg",    1000, 1000     },
	{ "video-mem",  1000, 15000000 },
};

static const char * const sm8250_clk_reset_table[] = { "bus", "core" };

static const struct bw_info sm8250_bw_table_dec[] = {
	{ ((4096 * 2160) / 256) * 60, 2403000 },
	{ ((4096 * 2160) / 256) * 30, 1224000 },
	{ ((1920 * 1080) / 256) * 60,  812000 },
	{ ((1920 * 1080) / 256) * 30,  416000 },
};

static const char * const sm8250_pmdomain_table[] = { "venus", "vcodec0" };

static const char * const sm8250_opp_pd_table[] = { "mx" };

static const struct platform_clk_data sm8250_clk_table[] = {
	{IRIS_AXI_CLK,  "iface"        },
	{IRIS_CTRL_CLK, "core"         },
	{IRIS_HW_CLK,   "vcodec0_core" },
};

static struct tz_cp_config tz_cp_config_sm8250 = {
	.cp_start = 0,
	.cp_size = 0x25800000,
	.cp_nonpixel_start = 0x01000000,
	.cp_nonpixel_size = 0x24800000,
};

static const u32 sm8250_vdec_input_config_param_default[] = {
	HFI_PROPERTY_CONFIG_VIDEOCORES_USAGE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
	HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM,
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_PARAM_BUFFER_SIZE_ACTUAL,
	HFI_PROPERTY_PARAM_BUFFER_ALLOC_MODE,
};

static const u32 sm8250_venc_input_config_param[] = {
	HFI_PROPERTY_CONFIG_FRAME_RATE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_INFO,
	HFI_PROPERTY_PARAM_FRAME_SIZE,
	HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT,
	HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL,
};

static const u32 sm8250_dec_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_SCRATCH_1,
};

static const u32 sm8250_dec_op_int_buf_tbl[] = {
	BUF_DPB,
};

static const u32 sm8250_enc_ip_int_buf_tbl[] = {
	BUF_BIN,
	BUF_SCRATCH_1,
	BUF_SCRATCH_2,
};

const struct iris_platform_data sm8250_data = {
	.get_instance = iris_hfi_gen1_get_instance,
	.init_hfi_command_ops = &iris_hfi_gen1_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen1_response_ops_init,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.vpu_ops = &iris_vpu2_ops,
	.set_preset_registers = iris_set_sm8250_preset_registers,
	.icc_tbl = sm8250_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8250_icc_table),
	.clk_rst_tbl = sm8250_clk_reset_table,
	.clk_rst_tbl_size = ARRAY_SIZE(sm8250_clk_reset_table),
	.bw_tbl_dec = sm8250_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sm8250_bw_table_dec),
	.pmdomain_tbl = sm8250_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8250_pmdomain_table),
	.opp_pd_tbl = sm8250_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sm8250_opp_pd_table),
	.clk_tbl = sm8250_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sm8250_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu-1.0/venus.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sm8250,
	.inst_fw_caps_dec = inst_fw_cap_sm8250_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8250_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8250_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8250_enc),
	.tz_cp_config_data = &tz_cp_config_sm8250,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.num_vpp_pipe = 4,
	.max_session_count = 16,
	.max_core_mbpf = NUM_MBS_8K,
	.max_core_mbps = ((7680 * 4320) / 256) * 60,
	.dec_input_config_params_default =
		sm8250_vdec_input_config_param_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8250_vdec_input_config_param_default),
	.enc_input_config_params = sm8250_venc_input_config_param,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8250_venc_input_config_param),

	.dec_ip_int_buf_tbl = sm8250_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8250_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_op_int_buf_tbl),

	.enc_ip_int_buf_tbl = sm8250_enc_ip_int_buf_tbl,
	.enc_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_enc_ip_int_buf_tbl),
};

/*
 * SC7280 can run either the Gen1 firmware (vpu20_p1.mbn) or the newer Gen2
 * one (vpu20_p1_gen2_s6.mbn). Mainline still defaults to Gen1; the Gen2
 * selection below mirrors the pending upstream series
 * "media: qcom: iris: Add generic Gen2 firmware detection and loading",
 * which reports a working encoder on this SoC. Gen1 cannot encode here - it
 * wedges the video bus hard enough to take the SoC down.
 */

/* QCM6490 UBWC settings, matching the vendor driver: UBWC_CONFIG(8,32,15,0,1,1,1). */
static struct ubwc_config_data ubwc_config_sc7280 = {
	.max_channels = 8,
	.mal_length = 32,
	.highest_bank_bit = 15,
	.bank_swzl_level = 0,
	.bank_swz2_level = 1,
	.bank_swz3_level = 1,
	.bank_spreading = 1,
};

bool iris_sc7280_gen2 = true;
module_param(iris_sc7280_gen2, bool, 0444);
MODULE_PARM_DESC(iris_sc7280_gen2, "use Gen2 firmware/HFI on SC7280");

const struct iris_platform_data sc7280_data = {
	.get_instance = iris_hfi_gen1_get_instance,
	.init_hfi_command_ops = &iris_hfi_gen1_command_ops_init,
	.init_hfi_response_ops = iris_hfi_gen1_response_ops_init,
	.get_vpu_buffer_size = iris_vpu_buf_size,
	.vpu_ops = &iris_vpu2_ops,
	.set_preset_registers = iris_set_sm8250_preset_registers,
	.icc_tbl = sm8250_icc_table,
	.icc_tbl_size = ARRAY_SIZE(sm8250_icc_table),
	.bw_tbl_dec = sc7280_bw_table_dec,
	.bw_tbl_dec_size = ARRAY_SIZE(sc7280_bw_table_dec),
	.pmdomain_tbl = sm8250_pmdomain_table,
	.pmdomain_tbl_size = ARRAY_SIZE(sm8250_pmdomain_table),
	.opp_pd_tbl = sc7280_opp_pd_table,
	.opp_pd_tbl_size = ARRAY_SIZE(sc7280_opp_pd_table),
	.clk_tbl = sc7280_clk_table,
	.clk_tbl_size = ARRAY_SIZE(sc7280_clk_table),
	/* Upper bound of DMA address range */
	.dma_mask = 0xe0000000 - 1,
	.fwname = "qcom/vpu/vpu20_p1.mbn",
	.pas_id = IRIS_PAS_ID,
	.inst_caps = &platform_inst_cap_sc7280,
	.inst_fw_caps_dec = inst_fw_cap_sm8250_dec,
	.inst_fw_caps_dec_size = ARRAY_SIZE(inst_fw_cap_sm8250_dec),
	.inst_fw_caps_enc = inst_fw_cap_sm8250_enc,
	.inst_fw_caps_enc_size = ARRAY_SIZE(inst_fw_cap_sm8250_enc),
	.tz_cp_config_data = &tz_cp_config_sm8250,
	.hw_response_timeout = HW_RESPONSE_TIMEOUT_VALUE,
	.num_vpp_pipe = 1,
	.no_aon = true,
	.max_session_count = 16,
	.max_core_mbpf = 4096 * 2176 / 256 * 2 + 1920 * 1088 / 256,
	/* max spec for SC7280 is 4096x2176@60fps */
	.max_core_mbps = 4096 * 2176 / 256 * 60,
	.dec_input_config_params_default =
		sm8250_vdec_input_config_param_default,
	.dec_input_config_params_default_size =
		ARRAY_SIZE(sm8250_vdec_input_config_param_default),
	.enc_input_config_params = sm8250_venc_input_config_param,
	.enc_input_config_params_size =
		ARRAY_SIZE(sm8250_venc_input_config_param),

	.dec_ip_int_buf_tbl = sm8250_dec_ip_int_buf_tbl,
	.dec_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_ip_int_buf_tbl),
	.dec_op_int_buf_tbl = sm8250_dec_op_int_buf_tbl,
	.dec_op_int_buf_tbl_size = ARRAY_SIZE(sm8250_dec_op_int_buf_tbl),

	.enc_ip_int_buf_tbl = sm8250_enc_ip_int_buf_tbl,
	.enc_ip_int_buf_tbl_size = ARRAY_SIZE(sm8250_enc_ip_int_buf_tbl),
};

/*
 * SC7280 running the Gen2 firmware.
 *
 * It is assembled at probe from two whole structs rather than written out
 * field by field: the Gen2 baseline brings every firmware-facing table
 * together with the size its defining file computed with ARRAY_SIZE(), so a
 * pointer can never end up paired with the wrong count. Only the fields that
 * describe the SoC itself are then taken back from the Gen1 SC7280 data.
 */
/*
 * Built per core rather than shared: a global would be a writable object that
 * every probed core keeps a pointer into, which is a data race waiting to
 * happen the moment there is more than one instance or a concurrent bind.
 */
struct iris_platform_data *iris_sc7280_gen2_platform_data(struct device *dev)
{
	struct iris_platform_data *pd;

	pd = devm_kmemdup(dev, &sm8550_data, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return NULL;

	/* HFI ops, property tables, caps and internal buffer lists come from
	 * the Gen2 baseline above; take back everything that describes SC7280
	 * rather than the firmware.
	 */

	/* Everything that describes SC7280 rather than the firmware. */
	pd->inst_caps = sc7280_data.inst_caps;
	pd->vpu_ops = sc7280_data.vpu_ops;
	pd->set_preset_registers = sc7280_data.set_preset_registers;
	pd->icc_tbl = sc7280_data.icc_tbl;
	pd->icc_tbl_size = sc7280_data.icc_tbl_size;
	pd->bw_tbl_dec = sc7280_data.bw_tbl_dec;
	pd->bw_tbl_dec_size = sc7280_data.bw_tbl_dec_size;
	pd->pmdomain_tbl = sc7280_data.pmdomain_tbl;
	pd->pmdomain_tbl_size = sc7280_data.pmdomain_tbl_size;
	pd->opp_pd_tbl = sc7280_data.opp_pd_tbl;
	pd->opp_pd_tbl_size = sc7280_data.opp_pd_tbl_size;
	pd->clk_tbl = sc7280_data.clk_tbl;
	pd->clk_tbl_size = sc7280_data.clk_tbl_size;
	pd->tz_cp_config_data = sc7280_data.tz_cp_config_data;
	pd->dma_mask = sc7280_data.dma_mask;
	pd->num_vpp_pipe = sc7280_data.num_vpp_pipe;
	pd->no_aon = sc7280_data.no_aon;
	pd->max_core_mbpf = sc7280_data.max_core_mbpf;
	pd->max_core_mbps = sc7280_data.max_core_mbps;

	/* SM8550 resets a "bus" clock that SC7280's video node does not have. */
	pd->clk_rst_tbl = NULL;
	pd->clk_rst_tbl_size = 0;

	/* UBWC is SoC specific: UBWC_CONFIG(8, 32, 15, 0, 1, 1, 1) on QCM6490. */
	pd->ubwc_config = &ubwc_config_sc7280;

	/* Verified against vpu20_p1_gen2_s6; see iris_hfi_gen2_command.c. */
	pd->rc_from_frame_rate = true;

	/* The Gen2 firmware for this VPU revision, and its buffer sizing. */
	pd->fwname = "qcom/vpu/vpu20_p1_gen2_s6.mbn";
	pd->get_vpu_buffer_size = iris_vpu33_buf_size;

	return pd;
}
