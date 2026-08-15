/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Backport shims for running the v6.19 iris driver on this v6.8 based kernel.
 *
 * Everything in this file compensates for core API that landed after v6.8.
 * It exists so the driver itself can stay byte-identical to upstream; delete
 * the file (and its includes) once the kernel is new enough to provide these.
 */
#ifndef __IRIS_COMPAT_H__
#define __IRIS_COMPAT_H__

#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>

/*
 * v6.13: of_reserved_mem_region_to_resource().
 * Resolve the memory-region phandle and translate it the long way round.
 */
static inline int iris_of_reserved_mem_region_to_resource(struct device_node *np,
							  unsigned int idx,
							  struct resource *res)
{
	struct device_node *target;
	int ret;

	target = of_parse_phandle(np, "memory-region", idx);
	if (!target)
		return -ENODEV;

	ret = of_address_to_resource(target, 0, res);
	of_node_put(target);

	return ret;
}
#define of_reserved_mem_region_to_resource iris_of_reserved_mem_region_to_resource

/*
 * v6.16: PD_FLAG_REQUIRED_OPP tells genpd to honour the required-opps of the
 * domains it attaches, so that raising the core clock also votes the
 * associated rails to a matching performance level. genpd here cannot do that
 * at attach time - the OPP core reaches those domains through virtual devices
 * instead, which is how venus drives the same rails on this kernel. Define the
 * flag as a bit genpd does not use so the attach shim below can tell the two
 * kinds of attach apart; dropping it silently would leave the rails at
 * whatever level the boot loader voted for.
 */
#ifndef PD_FLAG_REQUIRED_OPP
#define PD_FLAG_REQUIRED_OPP BIT(31)
#endif

/*
 * v6.12: devm_pm_domain_attach_list(). The non-devm variant already exists
 * here, so wrap it with a devm action that detaches on driver teardown.
 */
static inline void iris_pm_domain_detach_list(void *data)
{
	dev_pm_domain_detach_list(data);
}

static inline int iris_devm_pm_domain_attach_list(struct device *dev,
						  const struct dev_pm_domain_attach_data *data,
						  struct dev_pm_domain_list **list)
{
	int ret;

	if (data->pd_flags & PD_FLAG_REQUIRED_OPP) {
		struct device **virt_devs;
		const char **names;
		unsigned int i;

		/* devm_pm_opp_attach_genpd() expects a NULL terminated list. */
		names = devm_kcalloc(dev, data->num_pd_names + 1, sizeof(*names),
				     GFP_KERNEL);
		if (!names)
			return -ENOMEM;

		for (i = 0; i < data->num_pd_names; i++)
			names[i] = data->pd_names[i];

		*list = NULL;

		return devm_pm_opp_attach_genpd(dev, names, &virt_devs);
	}

	ret = dev_pm_domain_attach_list(dev, data, list);
	if (ret < 0)
		return ret;

	if (*list) {
		int err = devm_add_action_or_reset(dev, iris_pm_domain_detach_list,
						   *list);
		if (err)
			return err;
	}

	return ret;
}
#define devm_pm_domain_attach_list iris_devm_pm_domain_attach_list

/*
 * v6.14 added dev_pm_genpd_set_hwmode() together with the HW_CTRL_TRIGGER
 * gdsc flag, which defers handing a domain to hardware control until after
 * its clocks are running. This kernel only has the older static HW_CTRL, so
 * mvs0_gdsc is handed over the moment it is powered on and the vcodec clocks
 * can then no longer be enabled from software ("clock stuck" warnings).
 *
 * The venus driver works around the same limitation on this kernel by driving
 * the video wrapper's core power control directly, so do that here as well:
 * select software control before the clocks are enabled and hand the core
 * back to hardware afterwards.
 */
#define IRIS_WRAPPER_BASE_V6		0x000b0000
#define IRIS_WRAPPER_CORE_PWR_STATUS	(IRIS_WRAPPER_BASE_V6 + 0x80)
#define IRIS_WRAPPER_CORE_PWR_CONTROL	(IRIS_WRAPPER_BASE_V6 + 0x84)

/*
 * Remaining callers are the VPU3x power sequence and the power-off path; on
 * this platform the wrapper writes above already cover the transition, so the
 * genpd call has nothing left to do.
 */
static inline int dev_pm_genpd_set_hwmode(struct device *dev, bool enable)
{
	return 0;
}

static inline int iris_vpu_core_power_control(void __iomem *reg_base, bool sw)
{
	void __iomem *ctrl = reg_base + IRIS_WRAPPER_CORE_PWR_CONTROL;
	void __iomem *stat = reg_base + IRIS_WRAPPER_CORE_PWR_STATUS;
	u32 val;

	writel(sw ? 0 : 1, ctrl);

	return readl_poll_timeout(stat, val, !(val & BIT(1)) == !sw, 1, 100);
}

/*
 * v6.12 split the vb2 minimum into two fields. This kernel only has
 * min_queued_buffers, which additionally gates when start_streaming() is
 * called - mapping min_reqbufs_allocation onto it makes vb2 wait for that
 * many queued buffers before starting the stream, which deadlocks a decoder
 * that must send one header buffer and wait for the resolution event.
 *
 * The REQBUFS minimum is only an allocation hint, so the assignments are
 * dropped at their call sites instead; nothing to define here.
 */

/*
 * v6.15: v4l2_fh_add()/v4l2_fh_del() take the struct file and maintain
 * filp->private_data themselves; file_to_v4l2_fh() reads it back.
 */
static inline void iris_v4l2_fh_add(struct v4l2_fh *fh, struct file *filp)
{
	filp->private_data = fh;
	v4l2_fh_add(fh);
}

static inline void iris_v4l2_fh_del(struct v4l2_fh *fh, struct file *filp)
{
	v4l2_fh_del(fh);
	filp->private_data = NULL;
}

static inline struct v4l2_fh *file_to_v4l2_fh(struct file *filp)
{
	return filp->private_data;
}

#endif
