// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <dt-bindings/clock/qcom,sm8550-gcc.h>
#include <dt-bindings/clock/qcom,sm8450-videocc.h>

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sort.h>

#include "iris_core.h"
#include "resources.h"

static const struct bus_info plat_bus_table[] = {
	{ NULL, "iris-cnoc", 1000, 1000     },
	{ NULL, "iris-ddr",  1000, 15000000 },
};

static const char * const plat_pd_table[] = { "iris-ctl", "vcodec", NULL };
#define PD_COUNT 2

static const char * const plat_opp_pd_table[] = { "mxc", "mmcx", NULL };
#define OPP_PD_COUNT 2

static const struct clock_info plat_clk_table[] = {
	{ NULL, "gcc_video_axi0", GCC_VIDEO_AXI0_CLK, 0, 0 },
	{ NULL, "core_clk",       VIDEO_CC_MVS0C_CLK, 0, 0 },
	{ NULL, "vcodec_core",    VIDEO_CC_MVS0_CLK,  1, 0 },
};

static const char * const plat_clk_reset_table[] = { "video_axi_reset", NULL };
#define RESET_COUNT 1

static void iris_pd_release(void *res)
{
	struct device *pd = (struct device *)res;

	dev_pm_domain_detach(pd, true);
}

static int iris_pd_get(struct iris_core *core, struct power_domain_info *pdinfo)
{
	int ret;

	pdinfo->genpd_dev = dev_pm_domain_attach_by_name(core->dev, pdinfo->name);
	if (IS_ERR_OR_NULL(pdinfo->genpd_dev))
		ret = PTR_ERR(pdinfo->genpd_dev) ? : -ENODATA;

	ret = devm_add_action_or_reset(core->dev, iris_pd_release, (void *)pdinfo->genpd_dev);
	if (ret)
		return ret;

	return ret;
}

static void iris_opp_dl_release(void *res)
{
	struct device_link *link = (struct device_link *)res;

	device_link_del(link);
}

static int iris_opp_dl_get(struct device *dev, struct device *supplier)
{
	u32 flag = DL_FLAG_RPM_ACTIVE | DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS;
	struct device_link *link = NULL;
	int ret;

	link = device_link_add(dev, supplier, flag);
	if (!link)
		return -EINVAL;

	ret = devm_add_action_or_reset(dev, iris_opp_dl_release, (void *)link);

	return ret;
}

static int init_bus(struct iris_core *core)
{
	struct bus_info *binfo = NULL;
	u32 i = 0;

	core->bus_count = ARRAY_SIZE(plat_bus_table);
	core->bus_tbl = devm_kzalloc(core->dev,
				     sizeof(struct bus_info) * core->bus_count,
				     GFP_KERNEL);
	if (!core->bus_tbl)
		return -ENOMEM;

	for (i = 0; i < core->bus_count; i++) {
		binfo = &core->bus_tbl[i];
		binfo->name = plat_bus_table[i].name;
		binfo->bw_min_kbps = plat_bus_table[i].bw_min_kbps;
		binfo->bw_max_kbps = plat_bus_table[i].bw_max_kbps;
		binfo->icc = devm_of_icc_get(core->dev, binfo->name);
		if (IS_ERR(binfo->icc)) {
			dev_err(core->dev,
				"%s: failed to get bus: %s\n", __func__, binfo->name);
			return PTR_ERR(binfo->icc);
		}
	}

	return 0;
}

static int init_power_domains(struct iris_core *core)
{
	struct power_domain_info *pdinfo = NULL;
	struct device **opp_vdevs = NULL;
	int ret;
	u32 i;

	core->pd_count = PD_COUNT;
	core->power_domain_tbl = devm_kzalloc(core->dev,
					      sizeof(struct power_domain_info) * core->pd_count,
					      GFP_KERNEL);
	if (!core->power_domain_tbl)
		return -ENOMEM;

	for (i = 0; i < core->pd_count; i++) {
		pdinfo = &core->power_domain_tbl[i];
		pdinfo->name = plat_pd_table[i];
		ret = iris_pd_get(core, pdinfo);
		if (ret) {
			dev_err(core->dev,
				"%s: failed to get pd: %s\n", __func__, pdinfo->name);
			return ret;
		}
	}

	ret = devm_pm_opp_attach_genpd(core->dev, plat_opp_pd_table, &opp_vdevs);
	if (ret)
		return ret;

	for (i = 0; i < OPP_PD_COUNT; i++) {
		ret = iris_opp_dl_get(core->dev, opp_vdevs[i]);
		if (ret) {
			dev_err(core->dev, "%s: failed to create dl: %s\n",
				__func__, dev_name(opp_vdevs[i]));
			return ret;
		}
	}

	ret = devm_pm_opp_of_add_table(core->dev);
	if (ret) {
		dev_err(core->dev, "%s: failed to add opp table\n", __func__);
		return ret;
	}

	return ret;
}

static int init_clocks(struct iris_core *core)
{
	struct clock_info *cinfo = NULL;
	u32 i;

	core->clk_count = ARRAY_SIZE(plat_clk_table);
	core->clock_tbl = devm_kzalloc(core->dev,
				       sizeof(struct clock_info) * core->clk_count,
				       GFP_KERNEL);
	if (!core->clock_tbl)
		return -ENOMEM;

	for (i = 0; i < core->clk_count; i++) {
		cinfo = &core->clock_tbl[i];
		cinfo->name = plat_clk_table[i].name;
		cinfo->clk_id = plat_clk_table[i].clk_id;
		cinfo->has_scaling = plat_clk_table[i].has_scaling;
		cinfo->clk = devm_clk_get(core->dev, cinfo->name);
		if (IS_ERR(cinfo->clk)) {
			dev_err(core->dev,
				"%s: failed to get clock: %s\n", __func__, cinfo->name);
			return PTR_ERR(cinfo->clk);
		}
	}

	return 0;
}

static int init_reset_clocks(struct iris_core *core)
{
	struct reset_info *rinfo = NULL;
	u32 i = 0;

	core->reset_count = RESET_COUNT;
	core->reset_tbl = devm_kzalloc(core->dev,
				       sizeof(struct reset_info) * core->reset_count,
				       GFP_KERNEL);
	if (!core->reset_tbl)
		return -ENOMEM;

	for (i = 0; i < core->reset_count; i++) {
		rinfo = &core->reset_tbl[i];
		rinfo->name = plat_clk_reset_table[i];
		rinfo->rst = devm_reset_control_get(core->dev, rinfo->name);
		if (IS_ERR(rinfo->rst)) {
			dev_err(core->dev,
				"%s: failed to get reset clock: %s\n", __func__, rinfo->name);
			return PTR_ERR(rinfo->rst);
		}
	}

	return 0;
}

int init_resources(struct iris_core *core)
{
	int ret;

	ret = init_bus(core);
	if (ret)
		return ret;

	ret = init_power_domains(core);
	if (ret)
		return ret;

	ret = init_clocks(core);
	if (ret)
		return ret;

	ret = init_reset_clocks(core);

	return ret;
}
