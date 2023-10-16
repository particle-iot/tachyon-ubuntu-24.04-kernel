// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_instance.h"
#include "iris_helpers.h"
#include "platform_common.h"
#include "vpu_iris3_power.h"

u64 iris_calc_freq_iris3(struct iris_inst *inst, u32 data_size)
{
	u64 vsp_cycles = 0, vpp_cycles = 0, fw_cycles = 0;
	u64 fw_vpp_cycles = 0, bitrate = 0, freq = 0;
	u32 base_cycles = 0, fps, mbpf;
	u32 height = 0, width = 0;
	struct v4l2_format *inp_f;
	u32 mbs_per_second;

	inp_f = inst->fmt_src;
	width = max(inp_f->fmt.pix_mp.width, inst->crop.width);
	height = max(inp_f->fmt.pix_mp.height, inst->crop.height);

	mbpf = NUM_MBS_PER_FRAME(height, width);
	fps = inst->max_rate;
	mbs_per_second = mbpf * fps;

	fw_cycles = fps * inst->cap[MB_CYCLES_FW].value;
	fw_vpp_cycles = fps * inst->cap[MB_CYCLES_FW_VPP].value;

	vpp_cycles = mbs_per_second * inst->cap[MB_CYCLES_VPP].value /
		inst->cap[PIPE].value;
	vpp_cycles += max(vpp_cycles / 20, fw_vpp_cycles);

	if (inst->cap[PIPE].value > 1)
		vpp_cycles += div_u64(vpp_cycles * 59, 1000);

	base_cycles = inst->cap[MB_CYCLES_VSP].value;
	bitrate = fps * data_size * 8;
	vsp_cycles = bitrate;

	if (inst->codec == VP9) {
		vsp_cycles = div_u64(vsp_cycles * 170, 100);
	} else if (inst->cap[ENTROPY_MODE].value ==
			V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC) {
		vsp_cycles = div_u64(vsp_cycles * 135, 100);
	} else {
		base_cycles = 0;
		vsp_cycles = div_u64(vsp_cycles, 2);
	}
	vsp_cycles = div_u64(vsp_cycles * 21, 20);

	if (inst->cap[STAGE].value == STAGE_1)
		vsp_cycles = vsp_cycles * 3;

		vsp_cycles += mbs_per_second * base_cycles;

	freq = max3(vpp_cycles, vsp_cycles, fw_cycles);

	return freq;
}

int iris_calc_bw_iris3(struct iris_inst *inst, struct bus_vote_data *data)
{
	const struct bw_info *bw_tbl = NULL;
	unsigned int num_rows = 0;
	unsigned int i, mbs, mbps;
	struct iris_core *core;

	if (!data)
		return 0;

	core = inst->core;

	mbs = (ALIGN(data->height, 16) / 16) * (ALIGN(data->width, 16) / 16);
	mbps = mbs * data->fps;
	if (mbps == 0)
		return 0;

	bw_tbl = core->platform_data->bw_tbl_dec;
	num_rows = core->platform_data->bw_tbl_dec_size;

	if (!bw_tbl || num_rows == 0)
		return 0;

	for (i = 0; i < num_rows; i++) {
		if (i != 0 && mbps > bw_tbl[i].mbs_per_sec)
			break;

		if (is_10bit_colorformat(data->color_formats[0]))
			data->bus_bw = bw_tbl[i].bw_ddr_10bit;
		else
			data->bus_bw = bw_tbl[i].bw_ddr;
	}

	dev_info(core->dev, "bus_bw %llu\n", data->bus_bw);

	return 0;
}
