// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include "pci_platform.h"
#include "debug.h"

static struct cnss_msi_config msi_config = {
	.total_vectors = 32,
	.total_users = MSI_USERS,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 3, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 10, .base_vector = 3 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
		{ .name = "DP", .num_vectors = 18, .base_vector = 14 },
	},
};

#ifdef CONFIG_ONE_MSI_VECTOR
/**
 * All the user share the same vector and msi data
 * For MHI user, we need pass IRQ array information to MHI component
 * MHI_IRQ_NUMBER is defined to specify this MHI IRQ array size
 */
#define MHI_IRQ_NUMBER 3
static struct cnss_msi_config msi_config_one_msi = {
	.total_vectors = 1,
	.total_users = 4,
	.users = (struct cnss_msi_user[]) {
		{ .name = "MHI", .num_vectors = 1, .base_vector = 0 },
		{ .name = "CE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "WAKE", .num_vectors = 1, .base_vector = 0 },
		{ .name = "DP", .num_vectors = 1, .base_vector = 0 },
	},
};
#endif

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
	return 0;
}

int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
	return -EOPNOTSUPP;
}

int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
	return 0;
}

int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				u16 link_speed, u16 link_width)
{
	return 0;
}

int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				u32 rc_num, u16 link_speed)
{
	return 0;
}

int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	return 0;
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv) {}

int cnss_wlan_adsp_pc_enable(struct cnss_pci_data *pci_priv, bool control)
{
	return 0;
}

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
	return 0;
}

int cnss_pci_prevent_l1(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

void cnss_pci_allow_l1(struct device *dev)
{
}
EXPORT_SYMBOL(cnss_pci_allow_l1);

int _cnss_pci_get_reg_dump(struct cnss_pci_data *pci_priv,
			   u8 *buf, u32 len)
{
	return 0;
}

void cnss_pci_update_drv_supported(struct cnss_pci_data *pci_priv)
{
	pci_priv->drv_supported = false;
}

int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config;

	return 0;
}

#ifdef CONFIG_ONE_MSI_VECTOR
int cnss_pci_get_one_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config_one_msi;

	return 0;
}

bool cnss_pci_fallback_one_msi(struct cnss_pci_data *pci_priv,
			       int *num_vectors)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_msi_config *msi_config;

	cnss_pci_get_one_msi_assignment(pci_priv);
	msi_config = pci_priv->msi_config;
	if (!msi_config) {
		cnss_pr_err("one msi_config is NULL!\n");
		return false;
	}
	*num_vectors = pci_alloc_irq_vectors(pci_dev,
					     msi_config->total_vectors,
					     msi_config->total_vectors,
					     PCI_IRQ_MSI);
	if (*num_vectors < 0) {
		cnss_pr_err("Failed to get one MSI vector!\n");
		return false;
	}
	cnss_pr_dbg("request MSI one vector\n");

	return true;
}

bool cnss_pci_is_one_msi(struct cnss_pci_data *pci_priv)
{
	return pci_priv && pci_priv->msi_config &&
	       (pci_priv->msi_config->total_vectors == 1);
}

int cnss_pci_get_one_msi_mhi_irq_array_size(struct cnss_pci_data *pci_priv)
{
	return MHI_IRQ_NUMBER;
}

bool cnss_pci_is_force_one_msi(struct cnss_pci_data *pci_priv)
{
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;

	return test_bit(FORCE_ONE_MSI, &plat_priv->ctrl_params.quirks);
}
#else
int cnss_pci_get_one_msi_assignment(struct cnss_pci_data *pci_priv)
{
	return 0;
}

bool cnss_pci_fallback_one_msi(struct cnss_pci_data *pci_priv,
			       int *num_vectors)
{
	return false;
}

bool cnss_pci_is_one_msi(struct cnss_pci_data *pci_priv)
{
	return false;
}

int cnss_pci_get_one_msi_mhi_irq_array_size(struct cnss_pci_data *pci_priv)
{
	return 0;
}

bool cnss_pci_is_force_one_msi(struct cnss_pci_data *pci_priv)
{
	return false;
}
#endif

static int cnss_pci_smmu_fault_handler(struct iommu_domain *domain,
				       struct device *dev, unsigned long iova,
				       int flags, void *handler_token)
{
	struct cnss_pci_data *pci_priv = handler_token;

	cnss_fatal_err("SMMU fault happened with IOVA 0x%lx\n", iova);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	pci_priv->is_smmu_fault = true;
	cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	cnss_force_fw_assert(&pci_priv->pci_dev->dev);

	/* IOMMU driver requires -ENOSYS to print debug info. */
	return -ENOSYS;
}

int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device_node *of_node;
	struct resource *res;
	const char *iommu_dma_type;
	u32 addr_win[2];
	int ret = 0;

	of_node = of_parse_phandle(pci_dev->dev.of_node, "qcom,iommu-group", 0);
	if (!of_node)
		return ret;

	cnss_pr_dbg("Initializing SMMU\n");

	pci_priv->iommu_domain = iommu_get_domain_for_dev(&pci_dev->dev);
	ret = of_property_read_string(of_node, "qcom,iommu-dma",
				      &iommu_dma_type);
	if (!ret && !strcmp("fastmap", iommu_dma_type)) {
		cnss_pr_dbg("Enabling SMMU S1 stage\n");
		pci_priv->smmu_s1_enable = true;
		iommu_set_fault_handler(pci_priv->iommu_domain,
					cnss_pci_smmu_fault_handler, pci_priv);
		cnss_register_iommu_fault_handler_irq(pci_priv);
	}

	ret = of_property_read_u32_array(of_node,  "qcom,iommu-dma-addr-pool",
					 addr_win, ARRAY_SIZE(addr_win));
	if (ret) {
		cnss_pr_err("Invalid SMMU size window, err = %d\n", ret);
		of_node_put(of_node);
		return ret;
	}

	pci_priv->smmu_iova_start = addr_win[0];
	pci_priv->smmu_iova_len = addr_win[1];
	cnss_pr_dbg("smmu_iova_start: %pa, smmu_iova_len: 0x%zx\n",
		    &pci_priv->smmu_iova_start,
		    pci_priv->smmu_iova_len);

	res = platform_get_resource_byname(plat_priv->plat_dev, IORESOURCE_MEM,
					   "smmu_iova_ipa");
	if (res) {
		pci_priv->smmu_iova_ipa_start = res->start;
		pci_priv->smmu_iova_ipa_current = res->start;
		pci_priv->smmu_iova_ipa_len = resource_size(res);
		cnss_pr_dbg("smmu_iova_ipa_start: %pa, smmu_iova_ipa_len: 0x%zx\n",
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
	}

	pci_priv->iommu_geometry = of_property_read_bool(of_node,
							 "qcom,iommu-geometry");
	cnss_pr_dbg("iommu_geometry: %d\n", pci_priv->iommu_geometry);

	of_node_put(of_node);

	return 0;
}

#if IS_ENABLED(CONFIG_ARCH_QCOM)
/**
 * cnss_pci_of_reserved_mem_device_init() - Assign reserved memory region
 *                                          to given PCI device
 * @pci_priv: driver PCI bus context pointer
 *
 * This function shall call corresponding of_reserved_mem_device* API to
 * assign reserved memory region to PCI device based on where the memory is
 * defined and attached to (platform device of_node or PCI device of_node)
 * in device tree.
 *
 * Return: 0 for success, negative value for error
 */
int cnss_pci_of_reserved_mem_device_init(struct cnss_pci_data *pci_priv)
{
	struct device *dev_pci = &pci_priv->pci_dev->dev;
	int ret;

	/* Use of_reserved_mem_device_init_by_idx() if reserved memory is
	 * attached to platform device of_node.
	 */
	ret = of_reserved_mem_device_init(dev_pci);
	if (ret) {
		if (ret == -EINVAL)
			cnss_pr_vdbg("Ignore, no specific reserved-memory assigned\n");
		else
			cnss_pr_err("Failed to init reserved mem device, err = %d\n",
				    ret);
	}

	return ret;
}

int cnss_pci_wake_gpio_init(struct cnss_pci_data *pci_priv)
{
	return 0;
}

void cnss_pci_wake_gpio_deinit(struct cnss_pci_data *pci_priv)
{
}
#endif

void cnss_mhi_report_error(struct cnss_pci_data *pci_priv)
{
	cnss_pr_dbg("Not supported yet");
}

void cnss_pci_set_tme_support(struct mhi_controller *mhi_ctrl,
			      struct cnss_pci_data *pci_priv)
{
	cnss_pr_dbg("Not supported yet");
}

bool cnss_pci_is_sync_probe(void)
{
	return false;
}

bool cnss_should_suspend_pwroff(struct pci_dev *pci_dev)
{
	return false;
}

#define SOC_HW_VERSION_OFFS (0x224)
#define SOC_HW_VERSION_FAM_NUM_BMSK (0xF0000000)
#define SOC_HW_VERSION_FAM_NUM_SHFT (28)
#define SOC_HW_VERSION_DEV_NUM_BMSK (0x0FFF0000)
#define SOC_HW_VERSION_DEV_NUM_SHFT (16)
#define SOC_HW_VERSION_MAJOR_VER_BMSK (0x0000FF00)
#define SOC_HW_VERSION_MAJOR_VER_SHFT (8)
#define SOC_HW_VERSION_MINOR_VER_BMSK (0x000000FF)
#define SOC_HW_VERSION_MINOR_VER_SHFT (0)

int cnss_mhi_get_soc_info(struct mhi_controller *mhi_ctrl)
{
	u32 soc_info;
	int ret;

	ret = mhi_ctrl->read_reg(mhi_ctrl,
				 mhi_ctrl->regs + SOC_HW_VERSION_OFFS,
				 &soc_info);
	if (ret) {
		cnss_pr_err("failed to get soc info, ret %d\n", ret);
		return ret;
	}

	mhi_ctrl->family_number = (soc_info & SOC_HW_VERSION_FAM_NUM_BMSK) >>
		SOC_HW_VERSION_FAM_NUM_SHFT;
	mhi_ctrl->device_number = (soc_info & SOC_HW_VERSION_DEV_NUM_BMSK) >>
		SOC_HW_VERSION_DEV_NUM_SHFT;
	mhi_ctrl->major_version = (soc_info & SOC_HW_VERSION_MAJOR_VER_BMSK) >>
		SOC_HW_VERSION_MAJOR_VER_SHFT;
	mhi_ctrl->minor_version = (soc_info & SOC_HW_VERSION_MINOR_VER_BMSK) >>
		SOC_HW_VERSION_MINOR_VER_SHFT;
	return 0;
}
