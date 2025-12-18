/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __CAM_SOC_INFO_H__
#define __CAM_SOC_INFO_H__

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID		137

#define SMEM_SOCINFO_BUILD_ID_LENGTH	32
#define SMEM_SOCINFO_CHIP_ID_LENGTH	32

#define SMEM_DDR_BUILD_ID		603

#define MAX_IDX_CH 8

/* DDR info SMEM item structure */
struct ddr_part_details {
	u8 revision_id1[2];
	u8 revision_id2[2];
	u8 width[2];
	u8 density[2];
};

struct ddr_freq_table {
	__le32 freq_khz;
	u8 enable;
};

struct ddr_freq_plan_entry {
	struct ddr_freq_table ddr_freq[14];
	u8 num_ddr_freqs;
	__le32 *clk_period_address;
	__le32 max_nom_ddr_freq;
};

struct ddrinfo {
	u8 manuf_id;
	u8 device_type;
	struct ddr_part_details ddr_params[MAX_IDX_CH];
	struct ddr_freq_plan_entry ddr_freq_tbl;
	u8 num_channels;
	u8 num_ranks[2];
	u8 hbb[2][2];
};

#endif
