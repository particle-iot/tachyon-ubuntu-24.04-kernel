/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_OFFLINE_IFE_HW_MGR_H_
#define _CAM_OFFLINE_IFE_HW_MGR_H_


#define CAM_IFE_HW_STATE_STOPPED  0
#define CAM_IFE_HW_STATE_STARTING 1
#define CAM_IFE_HW_STATE_STARTED  2
#define CAM_IFE_HW_STATE_BUSY     3
#define CAM_IFE_HW_STATE_STOPPING 4

int cam_ife_mgr_free_in_proc_req(struct cam_ife_hw_mgr *ife_hw_mgr,
	uint32_t hw_id);
unsigned int cam_ife_mgr_flush_in_queue(struct cam_ife_hw_mgr *ife_hw_mgr,
	uint32_t ctx_idx,
	bool just_incomplete,
	int64_t req_id);
int cam_ife_mgr_enqueue_offline_update(void *hw_mgr_priv,
	void *prepare_hw_update_args);
int cam_ife_mgr_enqueue_offline_config(void *hw_mgr_priv,
	void *config_hw_args);
int cam_ife_mgr_required_hw(void *hw_mgr_priv, bool stop);
int cam_ife_mgr_check_start_processing(void *hw_mgr_priv,
	struct cam_ife_hw_mgr_ctx *hw_mgr_ctx);
int cam_ife_find_reconfig_required(void *hw_mgr_priv,
	struct cam_ife_hw_mgr_ctx *hw_mgr_ctx);
int cam_ife_mgr_update_irq_mask_affected_ctx_stream_grp(
	struct cam_ife_hw_mgr_ctx *ctx, int index, bool enable_irq, bool is_internal_start);

int cam_ife_mgr_disable_irq(struct cam_ife_hw_mgr_ctx *ctx);

int cam_ife_hw_mgr_start_ife_out_res_stream_grp(int grp_cfg_index,
	struct cam_ife_hw_mgr_ctx  *ife_ctx);

int cam_ife_hw_mgr_ife_src_start_hw_stream_grp(int grp_cfg_index,
	struct cam_ife_hw_mgr_ctx      *ife_ctx);

int cam_ife_mgr_csid_start_hw_stream_grp(struct cam_ife_hw_mgr_ctx *ife_ctx,
	int grp_cfg_index, bool is_internal_start);

int cam_ife_mgr_enable_irq(struct cam_ife_hw_mgr_ctx *ctx, bool is_internal_start);

bool cam_ife_hw_mgr_check_outport_supported_for_lite(uint32_t res_type);

int cam_convert_res_id_to_hw_path(int res_id, int csid_res_id);

int cam_ife_hw_mgr_update_vc_dt_stream_grp(struct cam_isp_hw_mgr_res *isp_res, int index,
		enum cam_ife_pix_path_res_id path_res_id, bool is_rdi_path);

int cam_ife_hw_mgr_acquire_res_stream_grp(struct cam_ife_hw_mgr_ctx *ife_ctx,
		struct cam_isp_in_port_generic_info *in_port, uint32_t *acquired_hw_id,
		uint32_t *acquired_hw_path, uint32_t *acquired_rdi_res, bool crop_enable);

int cam_ife_mgr_hw_validate_vc_dt_stream_grp(struct cam_isp_in_port_generic_info *in_port,
		uint32_t vc, uint32_t dt);

int cam_ife_hw_mgr_get_virtual_mapping_out_port(void *priv,
		uint32_t out_port_id, bool is_virtual_rdi);

int cam_ife_hw_mgr_res_stream_on_off_grp_cfg(struct cam_ife_hw_mgr_ctx *ife_ctx,
		void *hw_args, enum cam_ife_csid_halt_cmd csid_halt_type, bool is_start_hw,
		bool *per_port_feature_enable, bool *skip_hw_deinit);

int cam_ife_hw_mgr_free_hw_ctx(struct cam_ife_hw_mgr_ctx *ife_ctx);

int cam_ife_mgr_clear_sensor_stream_cfg_grp(uint32_t grp_cfg_idx);

int cam_ife_hw_mgr_get_virtual_mapping_out_port(void *priv,
		uint32_t out_port_id, bool is_virtual_rdi);

int cam_ife_mgr_get_active_hw_ctx_cnt(struct cam_ife_hw_mgr_ctx  *ife_ctx,
		struct cam_isp_hw_cmd_args *isp_hw_cmd_args);

int cam_ife_mgr_get_active_hw_ctx(struct cam_ife_hw_mgr_ctx *ctx,
		struct cam_isp_hw_cmd_args *isp_hw_cmd_args);

int cam_ife_mgr_update_sensor_grp_stream_cfg(void *hw_mgr_priv, void *hw_cfg_args);

int cam_ife_mgr_check_per_port_enable(struct cam_isp_in_port_generic_info *in_port);
#endif /* _CAM_OFFLINE_IFE_HW_MGR_H_*/
