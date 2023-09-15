/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _RESOURCES_H_
#define _RESOURCES_H_

struct bus_info {
	struct icc_path		*icc;
	const char		*name;
	u32			bw_min_kbps;
	u32			bw_max_kbps;
};

struct power_domain_info {
	struct device	*genpd_dev;
	const char	*name;
};

struct clock_info {
	struct clk	*clk;
	const char	*name;
	u32		clk_id;
	bool		has_scaling;
	u64		prev;
};

struct reset_info {
	struct reset_control	*rst;
	const char		*name;
};

int init_resources(struct iris_core *core);

#endif
