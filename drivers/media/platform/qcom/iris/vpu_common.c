// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "vpu_iris3.h"
#include "iris_core.h"
#include "iris_helpers.h"
#include "vpu_common.h"

int write_register(struct iris_core *core, u32 reg, u32 value)
{
	void __iomem *base_addr;
	int ret;

	ret = check_core_lock(core);
	if (ret)
		return ret;

	base_addr = core->reg_base;
	base_addr += reg;
	writel_relaxed(value, base_addr);

	/* Make sure value is written into the register */
	wmb();

	return ret;
}

int read_register(struct iris_core *core, u32 reg, u32 *value)
{
	void __iomem *base_addr;

	base_addr = core->reg_base;

	*value = readl_relaxed(base_addr + reg);

	/* Make sure value is read correctly from the register */
	rmb();

	return 0;
}

static const struct compat_handle compat_handle[] = {
	{
		.compat                  = "qcom,sm8550-iris",
		.init                    = init_iris3,
	},
};

int init_vpu(struct iris_core *core)
{
	struct device *dev = NULL;
	int i, ret = 0;

	dev = core->dev;

	for (i = 0; i < ARRAY_SIZE(compat_handle); i++) {
		if (of_device_is_compatible(dev->of_node, compat_handle[i].compat)) {
			ret = compat_handle[i].init(core);
			if (ret)
				return ret;
			break;
		}
	}

	if (i == ARRAY_SIZE(compat_handle))
		return -EINVAL;

	return ret;
}
