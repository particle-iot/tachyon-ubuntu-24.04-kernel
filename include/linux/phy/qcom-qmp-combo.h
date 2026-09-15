/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Qualcomm QMP USB3/DP combo PHY: hooks for the USB controller glue.
 *
 * For a four-lane DP sink the combo PHY hands its USB3 lanes to DisplayPort
 * ("DP-only"). While that lasts, and until the USB3 PCS has been brought back
 * up afterwards, there is no USB3 pipe clock and the dwc3 controller has to
 * run off the UTMI clock instead.
 *
 * qcom_qmp_combo_usb3_needs_utmi_clk() returns that condition. The notifier
 * is called with the same value as its action (1: switch to UTMI, 0: pipe
 * clock is back) whenever it changes, so a consumer can either poll at its own
 * (re)init points or react to the event; both see one and the same state.
 */
#ifndef __LINUX_PHY_QCOM_QMP_COMBO_H
#define __LINUX_PHY_QCOM_QMP_COMBO_H

#include <linux/errno.h>
#include <linux/kconfig.h>
#include <linux/notifier.h>

struct phy;

#if IS_REACHABLE(CONFIG_PHY_QCOM_QMP_COMBO)
bool qcom_qmp_combo_usb3_needs_utmi_clk(struct phy *phy);
int qcom_qmp_combo_usb3_register_dp_only_notifier(struct phy *phy,
						  struct notifier_block *nb);
int qcom_qmp_combo_usb3_unregister_dp_only_notifier(struct phy *phy,
						    struct notifier_block *nb);
#else
static inline bool qcom_qmp_combo_usb3_needs_utmi_clk(struct phy *phy)
{
	return false;
}

static inline int
qcom_qmp_combo_usb3_register_dp_only_notifier(struct phy *phy,
					      struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int
qcom_qmp_combo_usb3_unregister_dp_only_notifier(struct phy *phy,
						struct notifier_block *nb)
{
	return -ENODEV;
}
#endif

#endif /* __LINUX_PHY_QCOM_QMP_COMBO_H */
