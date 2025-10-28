// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>


#include "cam_isp_hw_mgr_intf.h"
#include "cam_isp_hw.h"
#include "cam_vfe_hw_intf.h"
#include "cam_sfe_hw_intf.h"
#include "cam_isp_packet_parser.h"
#include "cam_ife_hw_mgr.h"
#include "cam_ife_hw_mgr_addons.h"
#include "cam_packet_util.h"

extern struct cam_ife_hw_mgr_sensor_grp_cfg  g_ife_sns_grp_cfg;
extern struct cam_ife_hw_mgr g_ife_hw_mgr;
extern uint32_t max_ife_out_res;

int cam_ife_mgr_free_in_proc_req(struct cam_ife_hw_mgr *ife_hw_mgr,
					uint32_t hw_id)
{
	struct cam_ife_mgr_offline_in_queue *c_elem;
	struct cam_ife_mgr_offline_in_queue *c_elem_temp;

	list_for_each_entry_safe(c_elem, c_elem_temp,
			&ife_hw_mgr->in_proc_queue.list, list) {
		if (c_elem->hw_id == hw_id) {
			if (c_elem->request_id == 1 &&
					ife_hw_mgr->starting_offline_cnt) {
				ife_hw_mgr->starting_offline_cnt--;
			}
			list_del_init(&c_elem->list);
			kfree(c_elem);
		}
	}
	return 0;
}

unsigned int cam_ife_mgr_flush_in_queue(
	struct cam_ife_hw_mgr *ife_hw_mgr,
	uint32_t ctx_idx,
	bool just_incomplete,
	int64_t req_id)
{
	struct cam_ife_mgr_offline_in_queue *c_elem;
	struct cam_ife_mgr_offline_in_queue *c_elem_temp;
	int left = 0;

	list_for_each_entry_safe(c_elem, c_elem_temp,
			&ife_hw_mgr->input_queue.list, list) {
		if (req_id >= 0 && req_id != c_elem->request_id)
			continue;
		if (c_elem->ctx_idx == ctx_idx) {
			if (!(c_elem->ready && just_incomplete)) {
				CAM_DBG(CAM_ISP, "flush req %d %llu",
					c_elem->ctx_idx, c_elem->request_id);
				c_elem->prepare.packet = NULL;
				list_del_init(&c_elem->list);
				kfree(c_elem);
			} else {
				left++;
				CAM_DBG(CAM_ISP, "leave req %d %llu",
					c_elem->ctx_idx, c_elem->request_id);
			}
		}
	}
	return left;
}

int cam_ife_mgr_enqueue_offline_update(void *hw_mgr_priv,
	void *prepare_hw_update_args)
{
	struct cam_ife_hw_mgr *ife_hw_mgr        = hw_mgr_priv;
	struct cam_hw_prepare_update_args *prepare =
		(struct cam_hw_prepare_update_args *) prepare_hw_update_args;
	struct cam_ife_hw_mgr_ctx *hw_mgr_ctx =
		(struct cam_ife_hw_mgr_ctx *) prepare->ctxt_to_hw_map;
	struct cam_ife_mgr_offline_in_queue *c_elem;

	c_elem = kzalloc(sizeof(struct cam_ife_mgr_offline_in_queue),
			GFP_KERNEL);

	memcpy(&c_elem->prepare, prepare,
		sizeof(struct cam_hw_prepare_update_args));
	c_elem->request_id = prepare->packet->header.request_id;
	c_elem->ctx_idx = hw_mgr_ctx->ctx_idx;
	c_elem->ready = false;
	INIT_LIST_HEAD(&c_elem->list);

	mutex_lock(&ife_hw_mgr->ctx_mutex);
	list_add_tail(&c_elem->list, &ife_hw_mgr->input_queue.list);
	mutex_unlock(&ife_hw_mgr->ctx_mutex);
	return 0;
}

int cam_ife_mgr_enqueue_offline_config(void *hw_mgr_priv,
	void *config_hw_args)
{
	int rc = -EINVAL;
	int found = 0;
	struct cam_ife_mgr_offline_in_queue *c_elem, *print_elem;
	struct cam_ife_hw_mgr         *ife_hw_mgr = hw_mgr_priv;
	struct cam_hw_config_args     *cfg =
			(struct cam_hw_config_args *)config_hw_args;
	struct cam_ife_hw_mgr_ctx     *hw_mgr_ctx =
			(struct cam_ife_hw_mgr_ctx *) cfg->ctxt_to_hw_map;

	mutex_lock(&ife_hw_mgr->ctx_mutex);
	list_for_each_entry(c_elem, &ife_hw_mgr->input_queue.list, list) {
		if (c_elem->prepare.packet->header.request_id != cfg->request_id ||
			c_elem->ctx_idx != hw_mgr_ctx->ctx_idx) {
			CAM_DBG(CAM_ISP, "#INVALID# input queue# c_elem %p %p %llu %d cfg %llu %d",
				c_elem, hw_mgr_ctx->concr_ctx, c_elem->prepare.packet->header.request_id,
				c_elem->ctx_idx, cfg->request_id, hw_mgr_ctx->ctx_idx);
			continue;
		}
		rc = 0;
		memcpy(&c_elem->cfg, cfg,
				sizeof(struct cam_hw_config_args));
		c_elem->ready = true;
		found = 1;
		break;
	}

	//add log to scan input queue list
	if (found == 0) {
		list_for_each_entry(print_elem, &ife_hw_mgr->input_queue.list, list) {
			CAM_DBG(CAM_ISP,
				"#PRINT QUEUE# ife mgr ctx %p c_elem %p req id %llu ctx id %d",
				hw_mgr_ctx, print_elem, c_elem->prepare.packet->header.request_id,
				c_elem->ctx_idx);
		}
	}
	mutex_unlock(&ife_hw_mgr->ctx_mutex);
	return rc;
}

int cam_ife_mgr_check_start_processing(void *hw_mgr_priv,
		struct cam_ife_hw_mgr_ctx *hw_mgr_ctx)
{
	struct cam_ife_mgr_offline_in_queue   *c_elem;
	struct cam_ife_mgr_offline_in_queue   *c_elem_temp;
	struct cam_ife_hw_mgr_ctx             *run_hw_mgr_ctx;
	struct cam_ife_hw_mgr                 *ife_hw_mgr        = hw_mgr_priv;
	struct cam_ife_hw_concrete_ctx        *c_ctx = NULL;
	int rc = 0;
	uint32_t state;
	bool found = false;
	bool is_init_pkt;


	mutex_lock(&ife_hw_mgr->ctx_mutex);

	if (list_empty(&ife_hw_mgr->used_ctx_list)) {
		CAM_INFO(CAM_ISP, "Currently no ctx in use");
		mutex_unlock(&ife_hw_mgr->ctx_mutex);
		return rc;
	}

	list_for_each_entry(c_ctx, &ife_hw_mgr->used_ctx_list, list) {
		state = atomic_read(&c_ctx->ctx_state);
		if (!c_ctx->flags.is_offline ||
			(state != CAM_IFE_HW_STATE_STARTED &&
			state != CAM_IFE_HW_STATE_STARTING)) {
			CAM_DBG(CAM_ISP,
				"#REJECT# FAILED to get ife, current concr_ctx : ctx id %p : %d offline %d acquired_hw_id %d state %d",
				c_ctx, c_ctx->ctx_index,
				c_ctx->flags.is_offline,
				c_ctx->acquired_hw_id, state);
			continue;
		}

		cam_ife_mgr_free_in_proc_req(ife_hw_mgr,
						c_ctx->acquired_hw_id);

		found = false;
		list_for_each_entry_safe(c_elem, c_elem_temp,
				&ife_hw_mgr->input_queue.list, list) {
			is_init_pkt =
				((c_elem->prepare.packet->header.op_code + 1) &
					0xF) == CAM_ISP_PACKET_INIT_DEV;
			if (c_ctx->waiting_start &&
				c_elem->ctx_idx != c_ctx->start_ctx_idx) {
				CAM_DBG(CAM_ISP,
					"#REJECT#: WAITING start %d input queue elem ctx idx %d start cxt idx %d req id %llu",
					c_ctx->waiting_start,
					c_elem->ctx_idx,
					c_ctx->start_ctx_idx,
					c_elem->request_id);
				continue;
			}
			if (!c_elem->ready) {
				CAM_DBG(CAM_ISP, "#REJECT#: input queue elem ctx idx %d req id %llu NOT ready",
						c_elem->ctx_idx, c_elem->request_id);
				continue;
			}
			if (ife_hw_mgr->starting_offline_cnt == 0 &&
				c_elem->request_id == 0) {
				c_elem->hw_id = c_ctx->acquired_hw_id;
				list_del_init(&c_elem->list);
				list_add_tail(&c_elem->list,
					&ife_hw_mgr->in_proc_queue.list);
				CAM_DBG(CAM_ISP,
					"Skip init config for already started HW");
				continue;
			}
			if (c_elem->request_id !=
				c_elem->prepare.packet->header.request_id)
				CAM_ERR(CAM_ISP,
					"Request id mismatch. Packet recycled before use!");

			run_hw_mgr_ctx =
				&ife_hw_mgr->virt_ctx_pool[c_elem->ctx_idx];
			if (!run_hw_mgr_ctx->ctx_in_use)
				CAM_ERR(CAM_ISP, "UNUSED CONTEXT %d",
						c_elem->ctx_idx);
			run_hw_mgr_ctx->concr_ctx = c_ctx;
			c_elem->prepare.ctxt_to_hw_map = run_hw_mgr_ctx;
			c_elem->cfg.ctxt_to_hw_map = run_hw_mgr_ctx;
			found = true;
			list_del_init(&c_elem->list);
			c_elem->hw_id = c_ctx->acquired_hw_id;
			list_add_tail(&c_elem->list,
				&ife_hw_mgr->in_proc_queue.list);
			break;
		}
		CAM_DBG(CAM_ISP, "found %d offline inqueue elem %p ctx %d",
					found, c_elem, c_elem->ctx_idx);
		if (found) {
			/* For the zero request we do not get event,
			 * so restore state
			 */
			if (c_elem->cfg.request_id != 0) {
				c_ctx->waiting_start = false;
				atomic_set(&c_ctx->ctx_state,
						CAM_IFE_HW_STATE_BUSY);
			} else {
				c_ctx->waiting_start = true;
				c_ctx->start_ctx_idx = c_elem->ctx_idx;
			}

			c_ctx->served_ctx_w = 1 - c_ctx->served_ctx_w;
			c_ctx->served_ctx_id[c_ctx->served_ctx_w] =
							run_hw_mgr_ctx->ctx_idx;
			if (ife_hw_mgr->offline_reconfig)
				cam_ife_mgr_update_offline_ife_out(run_hw_mgr_ctx);

			rc = cam_ife_mgr_prepare_hw_update(hw_mgr_priv,
					&c_elem->prepare);
			c_elem->cfg.num_hw_update_entries =
				c_elem->prepare.num_hw_update_entries;
			rc = cam_ife_mgr_config_hw(hw_mgr_priv, &c_elem->cfg);
		}
	}

	mutex_unlock(&ife_hw_mgr->ctx_mutex);
	return rc;
}

static uint32_t cam_ife_mgr_clk_to_bw(uint32_t clk, uint32_t bpc)
{
	return clk * bpc;
}

static uint32_t cam_ife_mgr_bw_to_clk(uint32_t bw, uint32_t bpc)
{
	return bw / bpc;
}

static uint32_t cam_ife_mgr_calc_bw(struct cam_ife_mgr_bw_data *bw_data)
{
	uint32_t bw;

	bw = bw_data->width * bw_data->height * bw_data->framerate;
	switch (bw_data->format) {
	case CAM_FORMAT_MIPI_RAW_8:
		break;
	case CAM_FORMAT_MIPI_RAW_10:
		bw = 5 * (bw / 4);
		break;
	case CAM_FORMAT_MIPI_RAW_12:
	case CAM_FORMAT_MIPI_RAW_14:
		bw = 3 * (bw / 2);
		break;
	case CAM_FORMAT_MIPI_RAW_20:
		bw = 5 * (bw / 2);
		break;
	case CAM_FORMAT_MIPI_RAW_16:
	case CAM_FORMAT_PLAIN16_8:
	case CAM_FORMAT_PLAIN16_10:
	case CAM_FORMAT_PLAIN16_12:
	case CAM_FORMAT_PLAIN16_14:
	case CAM_FORMAT_PLAIN16_16:
		bw *= 2;
		break;
	default:
		CAM_ERR(CAM_ISP, "Unsupported format %d", bw_data->format);
		break;
	}
	return bw;
}

int cam_ife_validate_config(
	struct cam_isp_in_port_generic_info   *curr_in_port,
	struct cam_isp_in_port_generic_info   *prev_in_port)
{
	struct cam_isp_out_port_generic_info  *curr_out_port;
	struct cam_isp_out_port_generic_info  *prev_out_port;
	bool reconfig = false;
	int i;

	for (i = 0; i < curr_in_port->num_out_res; i++) {
		curr_out_port = &curr_in_port->data[i];
		prev_out_port = &prev_in_port->data[i];
		if (curr_out_port->res_type == prev_out_port->res_type) {
			if ((curr_out_port->width != prev_out_port->width) &&
				(curr_out_port->height != prev_out_port->height)) {
				reconfig = true;
				break;
			}
		} else {
			reconfig = true;
			break;
		}
	}
	return reconfig;
}

int cam_ife_find_reconfig_required(void *hw_mgr_priv,
	struct cam_ife_hw_mgr_ctx *hw_mgr_ctx)
{
	struct cam_ife_hw_mgr_ctx             *offline_hw_mgr_ctx;
	struct cam_ife_hw_mgr                 *ife_hw_mgr = hw_mgr_priv;
	bool reconfig = false;
	int i, j;

	for (i = 0; i < CAM_IFE_CTX_MAX; i++) {
		if (ife_hw_mgr->virt_ctx_pool[i].is_offline &&
			ife_hw_mgr->virt_ctx_pool[i].ctx_in_use) {
			offline_hw_mgr_ctx = &ife_hw_mgr->virt_ctx_pool[i];
			for (j = 0; j < hw_mgr_ctx->num_in_ports; j++) {
				if (cam_ife_validate_config(&hw_mgr_ctx->in_ports[j],
					&offline_hw_mgr_ctx->in_ports[j])) {
					CAM_DBG(CAM_ISP,"Offline ISP reconfig required ctx %d",
						offline_hw_mgr_ctx->ctx_idx);
					return true;
				}
			}
		}
	}
	return reconfig;
}

int cam_ife_mgr_required_hw(void *hw_mgr_priv, bool stop)
{
	struct cam_ife_hw_mgr          *ife_hw_mgr = hw_mgr_priv;
	uint32_t                        max_bw, current_bw;
	uint64_t                        total_bw;
	uint64_t                        nom_bw_per_hw;
	uint64_t                        max_bw_per_hw;

	int i, cnt, req_hw;

	nom_bw_per_hw = cam_ife_mgr_clk_to_bw(ife_hw_mgr->nom_clk_threshold,
				ife_hw_mgr->bytes_per_clk);
	max_bw_per_hw = cam_ife_mgr_clk_to_bw(ife_hw_mgr->max_clk_threshold,
				ife_hw_mgr->bytes_per_clk);

	total_bw = 0;
	max_bw = 0;
	cnt = 0;
	for (i = 0; i < CAM_IFE_CTX_MAX; i++) {
		if (ife_hw_mgr->virt_ctx_pool[i].ctx_in_use &&
				ife_hw_mgr->virt_ctx_pool[i].is_offline) {
			current_bw = cam_ife_mgr_calc_bw(
				&ife_hw_mgr->virt_ctx_pool[i].bw_data);
			if (current_bw > max_bw)
				max_bw = current_bw;
			total_bw += current_bw;
			cnt++;
	}
	}

	/* If only one context presents - we need to stop all HW*/
	if ((stop) && (cnt == 1))
		return 0;

	if (max_bw < nom_bw_per_hw)
		req_hw = (uint32_t)((total_bw + nom_bw_per_hw - 1) /
				nom_bw_per_hw);
	else {
		req_hw = (uint32_t)((total_bw + max_bw_per_hw - 1) /
				max_bw_per_hw);
	}

	if (req_hw > CAM_MAX_OFFLINE_HW)
		req_hw = CAM_MAX_OFFLINE_HW;

	if (total_bw / req_hw > max_bw)
		ife_hw_mgr->offline_clk =
			cam_ife_mgr_bw_to_clk(total_bw / req_hw,
				ife_hw_mgr->bytes_per_clk);
	else
		ife_hw_mgr->offline_clk =
			cam_ife_mgr_bw_to_clk(max_bw,
				ife_hw_mgr->bytes_per_clk);

	if (ife_hw_mgr->offline_clk > ife_hw_mgr->max_clk_threshold)
		ife_hw_mgr->offline_clk = ife_hw_mgr->max_clk_threshold;
	else if (ife_hw_mgr->offline_clk < ife_hw_mgr->min_clk_threshold)
		ife_hw_mgr->offline_clk = ife_hw_mgr->min_clk_threshold;

		/*
	 * make the offline SFE clock rate follow offline IFE clock
		 */
	ife_hw_mgr->offline_sfe_clk  = ife_hw_mgr->offline_clk;

	CAM_DBG(CAM_ISP,
			"Offline starting %d, CTXs %d, BW: %u needed %d IFEs @ :clk %d sfe clk %d",
			stop, cnt, total_bw,
			req_hw, ife_hw_mgr->offline_clk,
			ife_hw_mgr->offline_sfe_clk);

	return req_hw;
}

int cam_ife_mgr_get_rdi_stream_cfg_cnt(
	struct cam_isp_sensor_group_config   *sensor_grp_config,
	int                                   index)
{
	uint32_t cnt = 0;
	int i;

	for (i = 0; i < sensor_grp_config->stream_grp_cfg[index].stream_cfg_cnt; i++) {
		switch (sensor_grp_config->stream_grp_cfg[index].stream_cfg[i].path_id) {
		case CAM_ISP_VIRTUAL_RDI0_PATH:
		case CAM_ISP_VIRTUAL_RDI1_PATH:
		case CAM_ISP_VIRTUAL_RDI2_PATH:
		case CAM_ISP_VIRTUAL_RDI3_PATH:
		case CAM_ISP_VIRTUAL_RDI4_PATH:
		case CAM_ISP_VIRTUAL_RDI5_PATH:
			cnt++;
			CAM_DBG(CAM_ISP, "path_id: %d",
				sensor_grp_config->stream_grp_cfg[index].stream_cfg[i].path_id);
			break;
		default:
			CAM_DBG(CAM_ISP, "Not rdi path_id: %d",
				sensor_grp_config->stream_grp_cfg[index].stream_cfg[i].path_id);
		}
	}
	return cnt;
}

int cam_ife_mgr_update_vc_dt_sensor_stream_cfg(
	uint32_t                 path_id,
	uint32_t                 vc,
	uint32_t                 dt,
	int                      idx,
	int                      stream_idx)
{
	int   rc = -EINVAL;
	int   j;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;

	grp_cfg = g_ife_sns_grp_cfg.grp_cfg[idx];

	switch (path_id) {
	case CAM_ISP_PXL_PATH:
		if (grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_pxl) {
			CAM_ERR(CAM_ISP,
				"only 1 valid pxl vc-dt is accepted for given sensor:%d",
				grp_cfg->stream_cfg[stream_idx].sensor_id);
			return -EINVAL;
		}

		grp_cfg->stream_cfg[stream_idx].pxl_vc = vc;
		grp_cfg->stream_cfg[stream_idx].pxl_dt = dt;
		grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_pxl++;
		CAM_DBG(CAM_ISP,
			"Incrementing for g_ife_sns_grp_cfg.grp_cfg[%d]->stream_cfg[%d].num_valid_vc_dt_pxl: %d",
			idx, stream_idx, grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_pxl);
		rc = 0;
		break;
	case CAM_ISP_PPP_PATH:
		if (grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_ppp) {
			CAM_ERR(CAM_ISP,
				"only 1 valid ppp vc-dt is accepted for given sensor:%d",
				grp_cfg->stream_cfg[stream_idx].sensor_id);
			return -EINVAL;
		}

		grp_cfg->stream_cfg[stream_idx].ppp_vc = vc;
		grp_cfg->stream_cfg[stream_idx].ppp_dt = dt;
		grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_ppp++;
		CAM_DBG(CAM_ISP,
			"Incrementing for g_ife_sns_grp_cfg.grp_cfg[%d]->stream_cfg[%d].num_valid_vc_dt_ppp: %d",
			idx, stream_idx, grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_ppp);
		rc = 0;
		break;
	case CAM_ISP_LCR_PATH:
		if (grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_lcr) {
			CAM_ERR(CAM_ISP,
				"only 1 valid lcr vc-dt is accepted for given sensor:%d",
				grp_cfg->stream_cfg[stream_idx].sensor_id);
			return -EINVAL;
		}

		grp_cfg->stream_cfg[stream_idx].lcr_vc = vc;
		grp_cfg->stream_cfg[stream_idx].lcr_dt = dt;
		grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_lcr++;
		CAM_DBG(CAM_ISP,
			"Incrementing for g_ife_sns_grp_cfg.grp_cfg[%d]->stream_cfg[%d].num_valid_vc_dt_lcr: %d",
			idx, stream_idx, grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_lcr);
		rc = 0;
		break;
	case CAM_ISP_VIRTUAL_RDI0_PATH:
	case CAM_ISP_VIRTUAL_RDI1_PATH:
	case CAM_ISP_VIRTUAL_RDI2_PATH:
	case CAM_ISP_VIRTUAL_RDI3_PATH:
	case CAM_ISP_VIRTUAL_RDI4_PATH:
	case CAM_ISP_VIRTUAL_RDI5_PATH:
		for (j = 0; j < CAM_ISP_VC_DT_CFG; j++) {
			if ((!grp_cfg->stream_cfg[stream_idx].rdi_vc[j]) &&
				(!grp_cfg->stream_cfg[stream_idx].rdi_dt[j])) {
				grp_cfg->stream_cfg[stream_idx].rdi_vc[j] = vc;
				grp_cfg->stream_cfg[stream_idx].rdi_dt[j] = dt;
				grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_rdi++;
				CAM_DBG(CAM_ISP,
					"Incrementing for g_ife_sns_grp_cfg.grp_cfg[%d]->stream_cfg[%d].num_valid_vc_dt_rdi: %d",
					idx, stream_idx,
					grp_cfg->stream_cfg[stream_idx].num_valid_vc_dt_rdi);
				rc = 0;
				break;
			}
		}
		if (j == CAM_ISP_VC_DT_CFG) {
			CAM_ERR(CAM_ISP, "no free vc-dt available to update");
			rc = -EFAULT;
		}
		break;
	default:
		CAM_ERR(CAM_ISP, "invalid path_id :%d for sensor_id :%d",
			path_id,
			grp_cfg->stream_cfg[stream_idx].sensor_id);
		rc = -EFAULT;
		break;
	}

	return rc;
}

int cam_ife_mgr_check_for_previous_sensor_cfg(
	struct cam_isp_sensor_group_config   *sensor_grp_config,
	int                                   grp_idx,
	int                                   sensor_grp_stream_idx,
	int                                   stream_idx)
{
	int   rc = -EINVAL;
	int   i;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;
	struct cam_isp_stream_grp_config         *stream_grp_cfg;

	grp_cfg = g_ife_sns_grp_cfg.grp_cfg[grp_idx];
	stream_grp_cfg = &sensor_grp_config->stream_grp_cfg[sensor_grp_stream_idx];

	for (i = 0; i < grp_cfg->stream_cfg_cnt; i++) {
		if (grp_cfg->stream_cfg[i].sensor_id ==
			stream_grp_cfg->stream_cfg[stream_idx].sensor_id) {
			grp_cfg->stream_cfg[i].color_filter_arrangement +=
				stream_grp_cfg->stream_cfg[stream_idx].color_filter_arrangement;
			if (stream_grp_cfg->stream_cfg[stream_idx].color_filter_arrangement)
				grp_cfg->rdi_yuv_conversion_stream_cnt +=
				stream_grp_cfg->stream_cfg[stream_idx].color_filter_arrangement;
			rc = cam_ife_mgr_update_vc_dt_sensor_stream_cfg(
					stream_grp_cfg->stream_cfg[stream_idx].path_id,
					stream_grp_cfg->stream_cfg[stream_idx].vc,
					stream_grp_cfg->stream_cfg[stream_idx].dt, grp_idx, i);
			break;
		}
	}
	return rc;
}

int cam_ife_mgr_dump_sensor_grp_stream_cfg(void)
{
	int i, j, k;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;

	CAM_DBG(CAM_ISP, "num_grp_cfg :%d",
		g_ife_sns_grp_cfg.num_grp_cfg);

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;

		grp_cfg = g_ife_sns_grp_cfg.grp_cfg[i];
		CAM_DBG(CAM_ISP, "stream_cfg_cnt: %d", grp_cfg->stream_cfg_cnt);

		for (j = 0; j < grp_cfg->stream_cfg_cnt; j++) {
			CAM_DBG(CAM_ISP,
				"i:%d sensor_id:%d pxl :%d rdi :%d lcr:%d ppp:%d decode_fmt:%d color_filter_arrangement:%d",
				i, grp_cfg->stream_cfg[j].sensor_id,
				grp_cfg->stream_cfg[j].num_valid_vc_dt_pxl,
				grp_cfg->stream_cfg[j].num_valid_vc_dt_rdi,
				grp_cfg->stream_cfg[j].num_valid_vc_dt_lcr,
				grp_cfg->stream_cfg[j].num_valid_vc_dt_ppp,
				grp_cfg->stream_cfg[j].decode_format,
				grp_cfg->stream_cfg[j].color_filter_arrangement);

			if (grp_cfg->stream_cfg[j].num_valid_vc_dt_rdi) {
				for (k = 0; k < grp_cfg->stream_cfg[j].num_valid_vc_dt_rdi;
					k++) {
					CAM_DBG(CAM_ISP, "RDI VC_DT k: %d vc:%d dt: %d", k,
						grp_cfg->stream_cfg[j].rdi_vc[k],
						grp_cfg->stream_cfg[j].rdi_dt[k]);
				}
			}

			if (grp_cfg->stream_cfg[j].num_valid_vc_dt_pxl) {
				CAM_DBG(CAM_ISP, "PXL VC_DT vc:%d dt: %d",
					grp_cfg->stream_cfg[j].pxl_vc,
					grp_cfg->stream_cfg[j].pxl_dt);
			}
			if (grp_cfg->stream_cfg[j].num_valid_vc_dt_ppp) {
				CAM_DBG(CAM_ISP, "PPP VC_DT vc:%d dt: %d",
					grp_cfg->stream_cfg[j].ppp_vc,
					grp_cfg->stream_cfg[j].ppp_dt);
			}
			if (grp_cfg->stream_cfg[j].num_valid_vc_dt_lcr) {
				CAM_DBG(CAM_ISP, "LCR VC_DT vc:%d dt: %d",
					grp_cfg->stream_cfg[j].lcr_vc,
					grp_cfg->stream_cfg[j].lcr_dt);
			}
		}
	}
	return 0;
}

int cam_ife_mgr_clear_sensor_stream_cfg_grp(uint32_t grp_cfg_idx)
{
	kfree(g_ife_sns_grp_cfg.grp_cfg[grp_cfg_idx]->res_list_ife_out);
	g_ife_sns_grp_cfg.grp_cfg[grp_cfg_idx]->res_list_ife_out = NULL;
	kfree(g_ife_sns_grp_cfg.grp_cfg[grp_cfg_idx]);
	g_ife_sns_grp_cfg.grp_cfg[grp_cfg_idx] = NULL;
	g_ife_sns_grp_cfg.num_grp_cfg--;
	return 0;
}

struct cam_ife_hw_mgr_stream_grp_config *cam_ife_mgr_get_free_grp_cfg(uint32_t *grp_idx)
{
	int i;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		g_ife_sns_grp_cfg.grp_cfg[i] = kcalloc(CAM_ISP_STREAM_GROUP_CFG_MAX,
			sizeof(struct cam_ife_hw_mgr_stream_grp_config), GFP_KERNEL);
		if (!g_ife_sns_grp_cfg.grp_cfg[i]) {
			CAM_ERR(CAM_ISP, "Alloc failed for grp_cfg[%d]", i);
			return ERR_PTR(-ENOMEM);
		}
		break;
	}

	if (i == CAM_ISP_STREAM_GROUP_CFG_MAX) {
		CAM_ERR(CAM_ISP, "No uninitialized g_ife_sns_grp_cfg.grp_cfg found");
		return ERR_PTR(-ENOENT);
	}

	*grp_idx = i;
	return g_ife_sns_grp_cfg.grp_cfg[i];
}

static bool cam_ife_hw_mgr_check_grp_cfg(
	struct cam_isp_stream_grp_config *stream_grp_cfg)
{
	int i;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;
	bool rc = false;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		grp_cfg = g_ife_sns_grp_cfg.grp_cfg[i];
		if ((stream_grp_cfg->res_type == grp_cfg->res_type) &&
			(stream_grp_cfg->lane_type == grp_cfg->lane_type) &&
			(stream_grp_cfg->lane_num == grp_cfg->lane_num) &&
			(stream_grp_cfg->lane_cfg == grp_cfg->lane_cfg) &&
			(stream_grp_cfg->feature_mask == grp_cfg->feature_mask)) {
			rc = true;
			break;
		}
	}
	return rc;
}

static bool cam_ife_hw_mgr_check_sensor_id(uint32_t sensor_id, uint32_t *grp_idx)
{
	int i, j;
	bool rc = false;
	struct cam_ife_hw_mgr_stream_grp_config *grp_cfg;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;

		grp_cfg = g_ife_sns_grp_cfg.grp_cfg[i];
		for (j = 0; j < grp_cfg->stream_cfg_cnt; j++) {
			if (sensor_id == grp_cfg->stream_cfg[j].sensor_id) {
				rc = true;
				*grp_idx = i;
				break;
			}
		}
		if (rc)
			break;
	}
	return rc;
}

static int cam_ife_hw_mgr_check_for_duplicate_grp_info(
	struct cam_isp_stream_grp_config *stream_grp_cfg)
{
	int i;
	uint32_t grp_idx;
	bool found = false;

	for (i = 0; i < stream_grp_cfg->stream_cfg_cnt; i++) {
		found = cam_ife_hw_mgr_check_sensor_id(stream_grp_cfg->stream_cfg[i].sensor_id,
				&grp_idx);
		if (found) {
			if (g_ife_sns_grp_cfg.grp_cfg[grp_idx]->acquire_cnt) {
				CAM_ERR(CAM_ISP,
					"grp_cfg[%d] is already updated with sensor_id:%d and acquired with acquire_cnt:%d",
					grp_idx, stream_grp_cfg->stream_cfg[i].sensor_id,
					g_ife_sns_grp_cfg.grp_cfg[grp_idx]->acquire_cnt);
				return -EINVAL;
			}
			cam_ife_mgr_clear_sensor_stream_cfg_grp(grp_idx);
		}
	}
	return 0;
}

static void cam_ife_mgr_handle_sensor_grp_cfg_update_fail(
	struct cam_isp_sensor_group_config *sensor_grp_config, int sensor_grp_stream_idx)
{
	int i, j;
	uint32_t grp_idx;
	struct cam_isp_stream_grp_config *stream_grp_cfg;

	for (i = sensor_grp_stream_idx; i >= 0; i--) {
		stream_grp_cfg = &sensor_grp_config->stream_grp_cfg[i];
		for (j = 0; j < stream_grp_cfg->stream_cfg_cnt; j++) {
			if (cam_ife_hw_mgr_check_sensor_id(stream_grp_cfg->stream_cfg[i].sensor_id,
				&grp_idx))
				cam_ife_mgr_clear_sensor_stream_cfg_grp(grp_idx);
		}
	}
}

int cam_ife_mgr_update_sensor_grp_stream_cfg(void *hw_mgr_priv,
	void *hw_cfg_args)
{
	int rc = -EFAULT;
	int i, j;
	uint32_t grp_idx = 0;
	bool grp_found;
	struct cam_update_sensor_stream_cfg_cmd  *cfg_cmd = hw_cfg_args;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;
	struct cam_isp_hw_mgr_res                *res_list_ife_out;
	struct cam_isp_sensor_group_config       *sensor_grp_config = NULL;
	struct cam_isp_stream_grp_config         *stream_grp_cfg;

	sensor_grp_config = memdup_user(u64_to_user_ptr(cfg_cmd->cfg_handle),
		sizeof(struct cam_isp_sensor_group_config));
	if (IS_ERR(sensor_grp_config)) {
		CAM_ERR(CAM_ISP, "failed to copy sensor group config data from user, rc:%d",
		PTR_ERR(sensor_grp_config));
		return PTR_ERR(sensor_grp_config);
	}

	if (g_ife_sns_grp_cfg.num_grp_cfg > CAM_ISP_STREAM_GROUP_CFG_MAX ||
		sensor_grp_config->num_grp_cfg == 0 ||
		sensor_grp_config->num_grp_cfg > CAM_ISP_STREAM_GROUP_CFG_MAX) {
		CAM_ERR(CAM_ISP,
			"invalid g_ife_sns_grp_cfg.num_grp_cfg:%d or sensor_grp_config->num_grp_cfg:%d",
			g_ife_sns_grp_cfg.num_grp_cfg, sensor_grp_config->num_grp_cfg);
		kfree(sensor_grp_config);
		return rc;
	}

	for (i = 0; i < sensor_grp_config->num_grp_cfg; i++) {
		stream_grp_cfg = &sensor_grp_config->stream_grp_cfg[i];
		if (stream_grp_cfg->stream_cfg_cnt > CAM_ISP_STREAM_CFG_MAX) {
			CAM_ERR(CAM_ISP,
				"stream config count %d exceed max supported value %d for stream_grp_cfg idx:%d",
				stream_grp_cfg->stream_cfg_cnt, CAM_ISP_STREAM_CFG_MAX, i);
			return -EINVAL;
		}

		grp_found = cam_ife_hw_mgr_check_grp_cfg(stream_grp_cfg);
		if (grp_found) {
			rc = cam_ife_hw_mgr_check_for_duplicate_grp_info(stream_grp_cfg);
			if (rc) {
				CAM_ERR(CAM_ISP,
					"Invalid sensor group config data update request for stream_grp_cfg[%d]",
					i);
				kfree(sensor_grp_config);
				return rc;
			}
		}
		grp_cfg = cam_ife_mgr_get_free_grp_cfg(&grp_idx);
		if (IS_ERR(grp_cfg)) {
			CAM_ERR(CAM_ISP,
				"Failed to get free grp cfg for grp:%d sensor_grp_config->num_grp_cfg:%d g_ife_sns_grp_cfg.num_grp_cfg:%d",
				i, sensor_grp_config->num_grp_cfg, g_ife_sns_grp_cfg.num_grp_cfg);
			kfree(sensor_grp_config);
			return PTR_ERR(grp_cfg);
		}

		/*init res_list pool */
		INIT_LIST_HEAD(&grp_cfg->free_res_list);
		INIT_LIST_HEAD(&grp_cfg->res_ife_csid_list);
		INIT_LIST_HEAD(&grp_cfg->res_ife_src_list);
		mutex_init(&grp_cfg->lock);

		for (j = 0; j < CAM_IFE_HW_STREAM_GRP_RES_POOL_MAX; j++) {
			INIT_LIST_HEAD(
				&grp_cfg->res_pool[j].list);
			list_add_tail(
				&grp_cfg->res_pool[j].list,
				&grp_cfg->free_res_list);
		}

		grp_cfg->res_type     = stream_grp_cfg->res_type;
		grp_cfg->lane_type    = stream_grp_cfg->lane_type;
		grp_cfg->lane_num     = stream_grp_cfg->lane_num;
		grp_cfg->lane_cfg     = stream_grp_cfg->lane_cfg;
		grp_cfg->feature_mask = stream_grp_cfg->feature_mask;

		for (j = 0; j < stream_grp_cfg->stream_cfg_cnt; j++) {
			/*check if configuration is for previous sensor id */
			rc = cam_ife_mgr_check_for_previous_sensor_cfg(sensor_grp_config,
				grp_idx, i, j);
			if (!rc)
				continue;
			if (rc == -EFAULT)
				goto err;

			grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].sensor_id =
				stream_grp_cfg->stream_cfg[j].sensor_id;
			grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].decode_format =
				stream_grp_cfg->stream_cfg[j].decode_format;
			grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].color_filter_arrangement =
				stream_grp_cfg->stream_cfg[j].color_filter_arrangement;

			if (stream_grp_cfg->stream_cfg[j].color_filter_arrangement)
				grp_cfg->rdi_yuv_conversion_stream_cnt +=
					stream_grp_cfg->stream_cfg[j].color_filter_arrangement;

			rc = cam_ife_mgr_update_vc_dt_sensor_stream_cfg(
					stream_grp_cfg->stream_cfg[j].path_id,
					stream_grp_cfg->stream_cfg[j].vc,
					stream_grp_cfg->stream_cfg[j].dt, grp_idx,
					grp_cfg->stream_cfg_cnt);
			if (rc) {
				CAM_ERR(CAM_ISP,
					"Invalid path_id :%d sensor_id:%d valid_vc_dt [%d %d %d %d]",
					stream_grp_cfg->stream_cfg[j].path_id,
					stream_grp_cfg->stream_cfg[j].sensor_id,
					grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].num_valid_vc_dt_pxl,
					grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].num_valid_vc_dt_ppp,
					grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].num_valid_vc_dt_lcr,
					grp_cfg->stream_cfg[grp_cfg->stream_cfg_cnt].num_valid_vc_dt_rdi);
					rc = -EFAULT;
					goto err;
			}
			grp_cfg->stream_cfg_cnt++;
			if (grp_cfg->stream_cfg_cnt > CAM_ISP_STREAM_CFG_MAX) {
				CAM_ERR(CAM_ISP,
					"stream config count exceed max supported value");
				rc = -EFAULT;
				goto err;
			}
		}

		grp_cfg->rdi_stream_cfg_cnt =
				cam_ife_mgr_get_rdi_stream_cfg_cnt(sensor_grp_config, i);

		CAM_DBG(CAM_ISP,
			"rdi_stream_cfg_cnt : %d stream_cfg_cnt:%d rdi_yuv_conversion_stream_cnt:%d",
			grp_cfg->rdi_stream_cfg_cnt, grp_cfg->stream_cfg_cnt,
			grp_cfg->rdi_yuv_conversion_stream_cnt);

		grp_cfg->res_list_ife_out = kcalloc(max_ife_out_res,
			sizeof(struct cam_isp_hw_mgr_res), GFP_KERNEL);
		if (!grp_cfg->res_list_ife_out) {
			rc = -ENOMEM;
			CAM_ERR(CAM_ISP, "Alloc failed for ife out res list");
			goto err;
		}

		for (j = 0; j < max_ife_out_res; j++) {
			res_list_ife_out = &grp_cfg->res_list_ife_out[j];
			INIT_LIST_HEAD(&res_list_ife_out->list);
		}
		g_ife_sns_grp_cfg.num_grp_cfg++;
	}
	cam_ife_mgr_dump_sensor_grp_stream_cfg();

	kfree(sensor_grp_config);
	return rc;
err:
	cam_ife_mgr_handle_sensor_grp_cfg_update_fail(sensor_grp_config, i);
	kfree(sensor_grp_config);
	return rc;
}

bool cam_ife_hw_mgr_check_outport_supported_for_lite(
	uint32_t res_type)
{
	bool vfe_out_supported_lite = false;

	switch (res_type) {
	case CAM_ISP_IFE_OUT_RES_RDI_0:
	case CAM_ISP_IFE_OUT_RES_RDI_1:
	case CAM_ISP_IFE_OUT_RES_RDI_2:
	case CAM_ISP_IFE_OUT_RES_RDI_3:
	case CAM_ISP_IFE_LITE_OUT_RES_PREPROCESS_RAW:
	case CAM_ISP_IFE_LITE_OUT_RES_STATS_BG:
		vfe_out_supported_lite = true;
		break;
	default:
		vfe_out_supported_lite = false;
		CAM_DBG(CAM_ISP, "Invalid isp res id: %d not supported for lite target",
			res_type);
		break;
	}

	return vfe_out_supported_lite;
}

static int cam_ife_hw_mgr_update_vfe_res_data(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_hw_mgr_res           *hw_mgr_res,
	struct cam_vfe_acquire_args         *vfe_acquire,
	uint32_t cmd_type)
{
	uint32_t rc;
	struct cam_hw_intf		*hw_intf = NULL;
	struct cam_vfe_resource_update   res_update;
	struct cam_ife_hw_concrete_ctx   *c_ctx = ife_ctx->concr_ctx;

	hw_intf = hw_mgr_res->hw_res[0]->hw_intf;

	res_update.priv = c_ctx;
	res_update.res = hw_mgr_res;
	res_update.vfe_acquire = vfe_acquire;

	CAM_DBG(CAM_ISP, "ctx:%d res:%d", c_ctx->ctx_index, hw_mgr_res->hw_res[0]->res_id);
	rc = hw_intf->hw_ops.process_cmd(
			hw_intf->hw_priv,
			cmd_type,
			&res_update,
			sizeof(struct cam_vfe_resource_update));

	if (rc)
		CAM_ERR(CAM_ISP, "Failed to update resource data for ife-ctx: %d sensor_id :%d",
			c_ctx->ctx_index, c_ctx->sensor_id);

	return rc;
}

static int cam_ife_hw_mgr_update_csid_res_data(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_hw_mgr_res           *hw_mgr_res,
	struct cam_csid_hw_reserve_resource_args  *csid_acquire)
{
	int rc = 0;
	struct cam_hw_intf               *hw_intf = NULL;
	struct cam_csid_resource_update   res_update;
	struct cam_ife_hw_concrete_ctx   *c_ctx = ife_ctx->concr_ctx;
	struct cam_ife_hw_mgr            *ife_hw_mgr = c_ctx->hw_mgr;

	hw_intf = hw_mgr_res->hw_res[0]->hw_intf;

	res_update.priv = c_ctx;
	res_update.res = hw_mgr_res;
	res_update.csid_acquire = csid_acquire;

	if (ife_hw_mgr->csid_camif_irq_support && c_ctx->ctx_type != CAM_IFE_CTX_TYPE_SFE)
		csid_acquire->handle_camif_irq = true;

	CAM_DBG(CAM_ISP, "ife_ctx :%d res_:%d", c_ctx->ctx_index, csid_acquire->res_id);

	if (hw_intf && hw_intf->hw_ops.process_cmd) {
		rc = hw_intf->hw_ops.process_cmd(
			hw_intf->hw_priv,
			CAM_ISP_HW_CMD_UPDATE_CSID_RES_DATA,
			&res_update,
			sizeof(struct cam_csid_resource_update));

		if (rc)
			CAM_ERR(CAM_ISP,
				"Failed to update resource data for ife-ctx: %d sensor_id :%d",
				c_ctx->ctx_index, c_ctx->sensor_id);
	}

	return rc;
}

static int cam_ife_hw_mgr_update_res_virtual_mapping_table(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_hw_mgr_res           *hw_mgr_res,
	uint32_t out_port_res_id)
{
	int i;
	uint32_t vfe_out_res_id = 0;
	struct cam_ife_hw_concrete_ctx   *c_ctx = ife_ctx->concr_ctx;

	for (i = 0; i < CAM_ISP_STREAM_CFG_MAX; i++) {
		if (c_ctx->mapping_table.virtual_rdi[i])
			continue;

		c_ctx->mapping_table.virtual_rdi[i] = out_port_res_id;

		switch (hw_mgr_res->res_id) {
		case CAM_IFE_PIX_PATH_RES_RDI_0:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_0;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_1:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_1;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_2:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_2;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_3:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_3;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_4:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_4;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_5:
			vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_5;
			break;
		default:
			CAM_ERR(CAM_ISP, "invalid resource type :%d",
				hw_mgr_res->res_id);
			return -EINVAL;
		}
		c_ctx->mapping_table.acquired_rdi[i] = vfe_out_res_id;
		c_ctx->mapping_table.rdi_path_count++;

		CAM_DBG(CAM_ISP,
			"ctx:%u sensor_id:0x%x virtual_rdi :0x%x acquired_rdi :0x%x rdi_path-count:%d",
			c_ctx->ctx_index, c_ctx->sensor_id, out_port_res_id,
			vfe_out_res_id, c_ctx->mapping_table.rdi_path_count);

		break;
	}
	return 0;
}

static int cam_ife_hw_mgr_link_csid_pxl_resources(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	bool                                 crop_enable,
	bool                                 is_pxl_path,
	int  index)
{
	struct cam_isp_hw_mgr_res                *hw_mgr_res, *hw_mgr_res_tmp;
	struct cam_isp_hw_mgr_res                *csid_res;
	struct cam_hw_intf                       *hw_intf;
	struct cam_isp_out_port_generic_info     *out_port = NULL;
	struct cam_csid_hw_reserve_resource_args  csid_acquire = {0};
	enum cam_ife_pix_path_res_id              path_res_id;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	int i;
	bool per_port_feature_enable = false;
	int rc;

	if (is_pxl_path)
		path_res_id = CAM_IFE_PIX_PATH_RES_IPP;
	else
		path_res_id = CAM_IFE_PIX_PATH_RES_PPP;

	list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_tmp,
		&g_ife_sns_grp_cfg.grp_cfg[index]->res_ife_csid_list,
		list) {
		if (hw_mgr_res == NULL) {
			CAM_DBG(CAM_ISP, "skipping hw_res index:%d", index);
			continue;
		}

		if ((hw_mgr_res->res_id == path_res_id) &&
			(!hw_mgr_res->linked)) {
			for (i = 0; i < in_port->num_valid_vc_dt; i++) {
				if ((in_port->vc[i] == hw_mgr_res->vc) &&
					(in_port->dt[i] == hw_mgr_res->dt)) {
					hw_mgr_res->linked = true;
					per_port_feature_enable = true;
					break;
				}
			}
			if (i != in_port->num_valid_vc_dt)
				break;
		}
	}
	if (!per_port_feature_enable) {
		CAM_ERR(CAM_ISP,
			"No free csid resources available for %s path, ife-ctx: %d sensor_id: %d",
			is_pxl_path ? "ipp" : "ppp",
			c_ctx->ctx_index, c_ctx->sensor_id);
		return -ENODEV;
	}

	rc = cam_ife_hw_mgr_get_res(&c_ctx->free_res_list, &csid_res);
	if (rc) {
		CAM_ERR(CAM_ISP, "No more free hw mgr resource");
		return -ENODEV;
	}

	if (in_port->num_out_res) {
		out_port = &(in_port->data[0]);
		hw_mgr_res->is_secure = out_port->secure_mode;
	}
	hw_mgr_res->is_dual_isp = 0;

	csid_acquire.sync_mode = CAM_ISP_HW_SYNC_NONE;
	csid_acquire.res_type = CAM_ISP_RESOURCE_PIX_PATH;
	csid_acquire.res_id = path_res_id;
	csid_acquire.in_port = in_port;
	csid_acquire.out_port = in_port->data;
	csid_acquire.node_res = hw_mgr_res->hw_res[0];
	csid_acquire.event_cb = cam_ife_hw_mgr_event_handler;
	csid_acquire.cb_priv = ife_ctx;
	csid_acquire.crop_enable = crop_enable;
	csid_acquire.drop_enable = false;
	csid_acquire.per_port_acquire = false;

	csid_acquire.event_cb = cam_ife_hw_mgr_event_handler;
	csid_acquire.tasklet = c_ctx->common.tasklet_info;
	csid_acquire.cb_priv = ife_ctx;
	csid_acquire.cdm_ops = c_ctx->cdm_ops;
	csid_acquire.vc = hw_mgr_res->vc;
	csid_acquire.dt = hw_mgr_res->dt;
	csid_acquire.decode_format = hw_mgr_res->decode_format;

	rc = cam_ife_hw_mgr_update_csid_res_data(ife_ctx, hw_mgr_res, &csid_acquire);
	if (rc)
		goto end;

	hw_intf = hw_mgr_res->hw_res[0]->hw_intf;

	c_ctx->left_hw_idx = hw_intf->hw_idx;
	c_ctx->right_hw_idx = 0;
	c_ctx->buf_done_controller = csid_acquire.buf_done_controller;
	c_ctx->flags.need_csid_top_cfg = csid_acquire.need_top_cfg;

	*csid_res = *hw_mgr_res;
	cam_ife_hw_mgr_put_res(&c_ctx->res_list_ife_csid, &csid_res);
end:
	return rc;
}

static int cam_ife_hw_mgr_link_csid_rdi_resources(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	bool                                 crop_enable,
	int  index)
{
	struct cam_isp_hw_mgr_res                *hw_mgr_res, *hw_mgr_res_tmp;
	struct cam_isp_hw_mgr_res                *csid_res;
	struct cam_isp_out_port_generic_info     *out_port = NULL;
	struct cam_csid_hw_reserve_resource_args  rdi_csid_acquire;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	int i, j;
	bool per_port_feature_enable = false;
	int rc = -EINVAL;

	for (i = 0; i < in_port->num_out_res; i++) {
		per_port_feature_enable = false;
		out_port = &in_port->data[i];
		if (!cam_ife_hw_mgr_is_rdi_res(out_port->res_type))
			continue;

		list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_tmp,
			&g_ife_sns_grp_cfg.grp_cfg[index]->res_ife_csid_list,
			list) {
			if (((hw_mgr_res->hw_res[0]->res_id >=
				CAM_IFE_PIX_PATH_RES_RDI_0) &&
				(hw_mgr_res->hw_res[0]->res_id <=
				CAM_IFE_PIX_PATH_RES_RDI_5)) &&
				(!hw_mgr_res->linked)) {
				for (j = 0; j < in_port->num_valid_vc_dt; j++) {
					if ((in_port->vc[j] == hw_mgr_res->vc) &&
						(in_port->dt[j] == hw_mgr_res->dt)) {
						hw_mgr_res->linked = true;
						per_port_feature_enable = true;
						CAM_DBG(CAM_ISP, "res_id: %d ctx:%d",
							hw_mgr_res->hw_res[0]->res_id,
							c_ctx->ctx_index);
						break;
					}
				}
				if (j != in_port->num_valid_vc_dt)
					break;
			}
		}
		if (!per_port_feature_enable) {
			CAM_ERR(CAM_ISP,
				"No free csid resources available for rdi path, ife-ctx: %d sensor_id: %d",
				c_ctx->ctx_index, c_ctx->sensor_id);
			return -ENODEV;
		}

		CAM_DBG(CAM_ISP, "res_id:%d hw_res_id :%d", hw_mgr_res->res_id,
			hw_mgr_res->hw_res[0]->res_id);

		rc = cam_ife_hw_mgr_get_res(&c_ctx->free_res_list, &csid_res);
		if (rc) {
			CAM_ERR(CAM_ISP, "No more free hw mgr resource");
			return -ENODEV;
		}

		hw_mgr_res->is_secure = out_port->secure_mode;
		memset(&rdi_csid_acquire, 0, sizeof(rdi_csid_acquire));
		rdi_csid_acquire.res_id = hw_mgr_res->hw_res[0]->res_id;
		rdi_csid_acquire.res_type = CAM_ISP_RESOURCE_PIX_PATH;
		rdi_csid_acquire.in_port = in_port;
		rdi_csid_acquire.out_port = out_port;
		rdi_csid_acquire.node_res = hw_mgr_res->hw_res[0];
		rdi_csid_acquire.event_cb = cam_ife_hw_mgr_event_handler;
		rdi_csid_acquire.tasklet = c_ctx->common.tasklet_info;
		rdi_csid_acquire.cb_priv = ife_ctx;
		rdi_csid_acquire.cdm_ops = c_ctx->cdm_ops;
		rdi_csid_acquire.per_port_acquire = false;
		rdi_csid_acquire.vc = hw_mgr_res->vc;
		rdi_csid_acquire.dt = hw_mgr_res->dt;
		rdi_csid_acquire.decode_format = hw_mgr_res->decode_format;

		/*
		 * Enable RDI pixel drop by default. CSID will enable only for
		 * ver 480 HW to allow userspace to control pixel drop pattern.
		 */
		rdi_csid_acquire.drop_enable = true;
		rdi_csid_acquire.crop_enable = true;
		rdi_csid_acquire.sync_mode = CAM_ISP_HW_SYNC_NONE;

		rc = cam_ife_hw_mgr_update_csid_res_data(ife_ctx, hw_mgr_res,
			&rdi_csid_acquire);
		if (rc)
			goto end;

		cam_ife_hw_mgr_update_res_virtual_mapping_table(ife_ctx, hw_mgr_res,
			out_port->res_type);

		c_ctx->flags.need_csid_top_cfg = rdi_csid_acquire.need_top_cfg;
		hw_mgr_res->res_type = CAM_ISP_RESOURCE_PIX_PATH;
		hw_mgr_res->res_id = rdi_csid_acquire.res_id;
		hw_mgr_res->is_dual_isp = 0;
		hw_mgr_res->hw_res[1] = NULL;
		hw_mgr_res->use_wm_pack = rdi_csid_acquire.use_wm_pack;

		if (c_ctx->flags.is_rdi_only_context) {
			c_ctx->buf_done_controller =
				rdi_csid_acquire.buf_done_controller;
			c_ctx->left_hw_idx =
				hw_mgr_res->hw_res[0]->hw_intf->hw_idx;
			c_ctx->right_hw_idx = 0;
		}

		*csid_res = *hw_mgr_res;
		cam_ife_hw_mgr_put_res(&c_ctx->res_list_ife_csid, &csid_res);
	}
end:
	return rc;
}

static int cam_ife_hw_mgr_link_csid_resources(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	bool                                 crop_enable,
	int  index)
{
	int rc = -EINVAL;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;

	if (in_port->ipp_count || in_port->lcr_count) {
		rc = cam_ife_hw_mgr_link_csid_pxl_resources(ife_ctx,
			in_port, crop_enable, true, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"failed to link pxl resources for ife_ctx:%d sensor_id :%d",
				c_ctx->ctx_index, c_ctx->sensor_id);
			goto end;
		}
	}

	if (in_port->rdi_count) {
		rc = cam_ife_hw_mgr_link_csid_rdi_resources(ife_ctx, in_port, crop_enable, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"failed to link rdi resources for ife_ctx:%d sensor_id :%d",
				c_ctx->ctx_index, c_ctx->sensor_id);
			goto end;
		}
	}

	if (in_port->ppp_count) {
		if (!in_port->ipp_count)
			crop_enable = false;

		rc = cam_ife_hw_mgr_link_csid_pxl_resources(ife_ctx,
			in_port, crop_enable, false, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"failed to link pxl resources for ife_ctx:%d sensor_id :%d",
				c_ctx->ctx_index, c_ctx->sensor_id);
			goto end;
		}
	}

end:
	return rc;
}

int cam_convert_res_id_to_hw_path(int res_id, int csid_res_id)
{
	if (res_id == CAM_ISP_HW_VFE_IN_LCR) {
		return CAM_ISP_LCR_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_PDLIB) {
		return CAM_ISP_PPP_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_CAMIF) {
		if (csid_res_id == CAM_IFE_PIX_PATH_RES_IPP_1)
			return CAM_ISP_PXL1_PATH;
		else if (csid_res_id == CAM_IFE_PIX_PATH_RES_IPP_2)
			return CAM_ISP_PXL2_PATH;
		else
			return CAM_ISP_PXL_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI0) {
		return CAM_ISP_RDI0_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI1) {
		return CAM_ISP_RDI1_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI2) {
		return CAM_ISP_RDI2_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI3) {
		return CAM_ISP_RDI3_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI4) {
		return CAM_ISP_RDI4_PATH;
	} else if (res_id == CAM_ISP_HW_VFE_IN_RDI5) {
		return CAM_ISP_RDI5_PATH;
	}

	return 0;
}

static int cam_ife_hw_mgr_link_ife_src_resources(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	int  index, uint32_t *acquired_hw_id,
	uint32_t *acquired_hw_path)
{
	struct cam_isp_hw_mgr_res   *hw_mgr_res, *hw_mgr_res_tmp;
	struct cam_isp_hw_mgr_res   *csid_res;
	struct cam_isp_hw_mgr_res   *ife_src_res;
	struct cam_hw_intf          *hw_intf;
	struct cam_ife_hw_mgr       *ife_hw_mgr;
	struct cam_hw_info           *hw_info;
	struct cam_vfe_acquire_args  vfe_acquire;
	bool                         per_port_feature_enable = false;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	int rc = -EINVAL;

	list_for_each_entry(csid_res, &c_ctx->res_list_ife_csid, list) {
		if (csid_res->num_children && !in_port->lcr_count)
			continue;

		if (in_port->lcr_count && csid_res->res_id != CAM_IFE_PIX_PATH_RES_IPP)
			continue;

		if (csid_res->res_id == CAM_IFE_PIX_PATH_RES_PPP && !in_port->ppp_count)
			continue;

		ife_hw_mgr = c_ctx->hw_mgr;
		hw_intf = ife_hw_mgr->ife_devices[
			csid_res->hw_res[0]->hw_intf->hw_idx]->hw_intf;
		hw_info = hw_intf->hw_priv;

		vfe_acquire.rsrc_type = CAM_ISP_RESOURCE_VFE_IN;
		vfe_acquire.tasklet = c_ctx->common.tasklet_info;
		vfe_acquire.vfe_in.cdm_ops = c_ctx->cdm_ops;
		vfe_acquire.vfe_in.in_port = in_port;
		vfe_acquire.vfe_in.is_fe_enabled = c_ctx->flags.is_fe_enabled;
		vfe_acquire.vfe_in.is_offline = c_ctx->flags.is_offline;
		vfe_acquire.priv = ife_ctx;
		vfe_acquire.event_cb = cam_ife_hw_mgr_event_handler;
		vfe_acquire.vfe_in.sync_mode = CAM_ISP_HW_SYNC_NONE;
		vfe_acquire.vfe_in.is_dual = csid_res->is_dual_isp;

		vfe_acquire.vfe_in.handle_camif_irq = true;

		if (ife_hw_mgr->csid_camif_irq_support && c_ctx->ctx_type !=
			CAM_IFE_CTX_TYPE_SFE)
			vfe_acquire.vfe_in.handle_camif_irq = false;

		switch (csid_res->res_id) {
		case CAM_IFE_PIX_PATH_RES_IPP:
			if (!in_port->lcr_count)
				vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_CAMIF;
			else
				vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_LCR;
			break;
		case CAM_IFE_PIX_PATH_RES_PPP:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_PDLIB;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_0:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI0;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_1:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI1;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_2:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI2;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_3:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI3;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_4:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI4;
			break;
		case CAM_IFE_PIX_PATH_RES_RDI_5:
			vfe_acquire.vfe_in.res_id = CAM_ISP_HW_VFE_IN_RDI5;
			break;
		default:
			CAM_ERR(CAM_ISP, "Wrong IFE CSID Path Resource ID : %d",
				csid_res->res_id);
			goto err;
		}

		list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_tmp,
			&g_ife_sns_grp_cfg.grp_cfg[index]->res_ife_src_list, list) {
			if ((hw_mgr_res->res_id == vfe_acquire.vfe_in.res_id) &&
				(!hw_mgr_res->linked)) {
				hw_mgr_res->linked = true;
				per_port_feature_enable = true;
				break;
			}
		}

		if (!per_port_feature_enable) {
			CAM_ERR(CAM_ISP,
				"No free ife_src resources available, ife-ctx: %d sensor_id: %d res_id: %d",
				c_ctx->ctx_index, c_ctx->sensor_id, vfe_acquire.vfe_in.res_id);
			return -ENODEV;
		}

		rc = cam_ife_hw_mgr_get_res(&c_ctx->free_res_list,
			&ife_src_res);
		if (rc) {
			CAM_ERR(CAM_ISP, "No more free hw mgr resource");
			goto err;
		}

		hw_mgr_res->res_type = vfe_acquire.rsrc_type;
		hw_mgr_res->is_dual_isp = csid_res->is_dual_isp;
		hw_mgr_res->use_wm_pack = csid_res->use_wm_pack;

		rc = cam_ife_hw_mgr_update_vfe_res_data(ife_ctx, hw_mgr_res, &vfe_acquire,
				CAM_ISP_HW_CMD_UPDATE_VFE_SRC_RES_DATA);
		if (rc)
			goto err;

		ife_hw_mgr = c_ctx->hw_mgr;
		hw_intf = ife_hw_mgr->ife_devices[csid_res->hw_res[0]->hw_intf->hw_idx]->hw_intf;
		*acquired_hw_id |= cam_convert_hw_idx_to_ife_hw_num(hw_intf->hw_idx);
		acquired_hw_path[0] |= cam_convert_res_id_to_hw_path(hw_mgr_res->hw_res[0]->res_id,
			csid_res->res_id);

		*ife_src_res = *hw_mgr_res;
		cam_ife_hw_mgr_put_res(&c_ctx->res_list_ife_src, &ife_src_res);
		csid_res->num_children++;
	}
err:
	return rc;
}

int cam_ife_hw_mgr_get_virtual_mapping_out_port(
	void                             *priv,
	uint32_t                         out_port_id,
	bool                             is_virtual_rdi)
{
	int i;
	int out_port = -1;
	struct cam_ife_hw_mgr_ctx        *ife_ctx;

	ife_ctx = (struct cam_ife_hw_mgr_ctx *)priv;

	if (!cam_ife_hw_mgr_is_rdi_res(out_port_id))
		return out_port_id;

	for (i = 0; i < ife_ctx->concr_ctx->mapping_table.rdi_path_count; i++) {
		if (is_virtual_rdi) {
			if (ife_ctx->concr_ctx->mapping_table.virtual_rdi[i] == out_port_id) {
				out_port = ife_ctx->concr_ctx->mapping_table.acquired_rdi[i];
				break;
			}
		} else {
			if (ife_ctx->concr_ctx->mapping_table.acquired_rdi[i] == out_port_id) {
				out_port = ife_ctx->concr_ctx->mapping_table.virtual_rdi[i];
				break;
			}
		}
	}

	if (out_port < 0) {
		CAM_ERR(CAM_ISP,
			"No match found, ife_ctx : %d sensor_id :%d rdi_path_cnt:%d %s_out_port 0x%x",
			ife_ctx->concr_ctx->ctx_index, ife_ctx->concr_ctx->sensor_id,
			ife_ctx->concr_ctx->mapping_table.rdi_path_count,
			is_virtual_rdi ? "Virtual" : "Acquired", out_port_id);
		return -EINVAL;
	}

	CAM_DBG(CAM_ISP,
		"ctx: %d, sensor_id: %d rdi_path_cnt:%d virtual_rdi: 0x%x acquired_rdi :0x%x",
		ife_ctx->concr_ctx->ctx_index, ife_ctx->concr_ctx->sensor_id,
		ife_ctx->concr_ctx->mapping_table.rdi_path_count,
		is_virtual_rdi ? out_port_id : out_port,
		is_virtual_rdi ? out_port : out_port_id);

	return out_port;
}

static int cam_ife_hw_mgr_link_res_ife_out_rdi(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_hw_mgr_res           *ife_src_res,
	struct cam_isp_in_port_generic_info *in_port,
	int    index)
{
	int rc = -EINVAL;
	struct cam_isp_out_port_generic_info     *out_port = NULL;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_hw_mgr_res                *ife_out_res_tmp, *ife_out_res;
	struct cam_vfe_acquire_args               vfe_acquire;
	struct cam_isp_context_comp_record       *comp_grp = NULL;
	uint32_t                                  comp_index;
	uint32_t  i, vfe_out_res_id, vfe_in_res_id;
	int out_port_res_type = -EINVAL;

	/* take left resource */
	vfe_in_res_id = ife_src_res->hw_res[0]->res_id;

	switch (vfe_in_res_id) {
	case CAM_ISP_HW_VFE_IN_RDI0:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_0;
		break;
	case CAM_ISP_HW_VFE_IN_RDI1:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_1;
		break;
	case CAM_ISP_HW_VFE_IN_RDI2:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_2;
		break;
	case CAM_ISP_HW_VFE_IN_RDI3:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_3;
		break;
	case CAM_ISP_HW_VFE_IN_RDI4:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_4;
		break;
	case CAM_ISP_HW_VFE_IN_RDI5:
		vfe_out_res_id = CAM_ISP_IFE_OUT_RES_RDI_5;
		break;
	default:
		CAM_ERR(CAM_ISP, "invalid resource type");
		goto err;
	}
	CAM_DBG(CAM_ISP, "vfe_in_res_id = %d, vfe_out_red_id = %d",
		vfe_in_res_id, vfe_out_res_id);

	for (i = 0; i < in_port->num_out_res; i++) {
		out_port = &in_port->data[i];

		if (!cam_ife_hw_mgr_is_rdi_res(out_port->res_type)) {
			CAM_INFO(CAM_ISP, "out_res: %d", out_port->res_type);
			continue;
		}

		out_port_res_type = cam_ife_hw_mgr_get_virtual_mapping_out_port(ife_ctx,
			out_port->res_type, true);

		if (out_port_res_type < 0)
			goto err;

		if (vfe_out_res_id != out_port_res_type)
			continue;
		else {
			CAM_DBG(CAM_ISP, "Matched 0x%x", vfe_out_res_id);
			break;
		}
	}

	if (i == in_port->num_out_res || (out_port_res_type < 0)) {
		CAM_ERR(CAM_ISP,
			"Cannot acquire out resource, i=%d, num_out_res=%d out_port_res_type:%d",
			i, in_port->num_out_res, out_port_res_type);
		goto err;
	}

	ife_out_res_tmp =
		&g_ife_sns_grp_cfg.grp_cfg[index]->res_list_ife_out[vfe_out_res_id & 0xFF];

	ife_out_res = &c_ctx->res_list_ife_out[c_ctx->num_acq_vfe_out];

	if (ife_out_res_tmp->hw_res[0] == NULL || ife_out_res_tmp->linked) {
		CAM_ERR(CAM_ISP,
			"no free ife_out RDI resource available ife_ctx: %d sensor_id: %d linked:%d",
			c_ctx->ctx_index, c_ctx->sensor_id, ife_out_res_tmp->linked);
		rc = -ENODEV;
		goto err;
	}

	vfe_acquire.rsrc_type = CAM_ISP_RESOURCE_VFE_OUT;
	vfe_acquire.tasklet = c_ctx->common.tasklet_info;
	vfe_acquire.vfe_out.cdm_ops = c_ctx->cdm_ops;
	vfe_acquire.priv = ife_ctx;
	vfe_acquire.vfe_out.out_port_info = out_port;
	vfe_acquire.vfe_out.out_port_info->acquired_res_type = out_port_res_type;
	vfe_acquire.vfe_out.split_id = CAM_ISP_HW_SPLIT_LEFT;
	vfe_acquire.vfe_out.unique_id = c_ctx->ctx_index;
	vfe_acquire.vfe_out.is_dual = 0;
	vfe_acquire.vfe_out.disable_ubwc_comp =
		g_ife_hw_mgr.debug_cfg.disable_ubwc_comp;
	vfe_acquire.event_cb = cam_ife_hw_mgr_event_handler;
	vfe_acquire.buf_done_controller = c_ctx->buf_done_controller;
	vfe_acquire.vfe_out.use_wm_pack = ife_src_res->use_wm_pack;

	rc = cam_ife_hw_mgr_update_vfe_res_data(ife_ctx, ife_out_res_tmp, &vfe_acquire,
			CAM_ISP_HW_CMD_UPDATE_VFE_OUT_RES_DATA);
	if (rc)
		goto err;

	ife_out_res_tmp->res_id = out_port_res_type;
	ife_out_res_tmp->res_type = CAM_ISP_RESOURCE_VFE_OUT;
	ife_out_res_tmp->linked = true;

	if (out_port->secure_mode)
		ife_out_res->is_secure = true;
	ife_out_res->hw_res[0] = ife_out_res_tmp->hw_res[0];
	ife_out_res->is_dual_isp = 0;
	ife_out_res->use_wm_pack = ife_src_res->use_wm_pack;
	ife_out_res->res_id = out_port_res_type;
	ife_out_res->res_type = CAM_ISP_RESOURCE_VFE_OUT;
	ife_out_res->linked = true;

	comp_index = vfe_acquire.vfe_out.comp_grp_id;
	comp_grp = &c_ctx->vfe_bus_comp_grp[comp_index];
	comp_grp->res_id[comp_grp->num_res] = ife_out_res->res_id;
	comp_grp->num_res++;

	c_ctx->vfe_out_map[vfe_out_res_id & 0xFF] = c_ctx->num_acq_vfe_out;
	ife_src_res->num_children++;
	c_ctx->num_acq_vfe_out++;

	return 0;
err:
	return rc;

}

static int cam_ife_hw_mgr_link_res_ife_out_pixel(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_hw_mgr_res           *ife_src_res,
	struct cam_isp_in_port_generic_info *in_port,
	int                                  index)
{
	int rc = -1;
	uint32_t  i, k;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_out_port_generic_info     *out_port;
	struct cam_isp_hw_mgr_res                *ife_out_res_tmp, *ife_out_res;
	struct cam_vfe_acquire_args               vfe_acquire;

	for (i = 0; i < in_port->num_out_res; i++) {
		out_port = &in_port->data[i];
		/* Skip output ports for SFE */
		if (!cam_ife_hw_mgr_is_ife_out_port(out_port->res_type))
			continue;

		if (cam_ife_hw_mgr_is_rdi_res(out_port->res_type))
			continue;

		if (!cam_ife_hw_mgr_check_path_port_compat(ife_src_res->res_id,
			out_port->res_type))
			continue;

		CAM_DBG(CAM_ISP, "res_type 0x%x", out_port->res_type);

		k = out_port->res_type & 0xFF;
		ife_out_res_tmp = &g_ife_sns_grp_cfg.grp_cfg[index]->res_list_ife_out[k];

		ife_out_res = &c_ctx->res_list_ife_out[k];

		if (ife_out_res_tmp->hw_res[0] == NULL || ife_out_res_tmp->linked) {
			CAM_ERR(CAM_ISP,
				"no free ife_out res_type:0x%x resource available ife_ctx: %d sensor_id: %d",
				out_port->res_type, c_ctx->ctx_index, c_ctx->sensor_id);
			rc = -ENODEV;
			goto err;
		}

		vfe_acquire.rsrc_type = CAM_ISP_RESOURCE_VFE_OUT;
		vfe_acquire.tasklet = c_ctx->common.tasklet_info;
		vfe_acquire.vfe_out.cdm_ops = c_ctx->cdm_ops;
		vfe_acquire.priv = ife_ctx;
		vfe_acquire.vfe_out.out_port_info =  out_port;
		vfe_acquire.vfe_out.is_dual       = ife_src_res->is_dual_isp;
		vfe_acquire.vfe_out.unique_id     = c_ctx->ctx_index;
		vfe_acquire.vfe_out.disable_ubwc_comp =
			g_ife_hw_mgr.debug_cfg.disable_ubwc_comp;
		vfe_acquire.event_cb = cam_ife_hw_mgr_event_handler;
		vfe_acquire.buf_done_controller = c_ctx->buf_done_controller;
		vfe_acquire.vfe_out.split_id  = CAM_ISP_HW_SPLIT_LEFT;
		vfe_acquire.vfe_out.is_master = 0;
		vfe_acquire.vfe_out.dual_slave_core = 0;

		CAM_DBG(CAM_ISP, "resource type :0x%x res id:0x%x",
				ife_out_res_tmp->hw_res[0]->res_type,
				ife_out_res_tmp->hw_res[0]->res_id);

		rc = cam_ife_hw_mgr_update_vfe_res_data(ife_ctx, ife_out_res_tmp, &vfe_acquire,
				CAM_ISP_HW_CMD_UPDATE_VFE_OUT_RES_DATA);
		if (rc)
			goto err;

		ife_out_res_tmp->res_type = CAM_ISP_RESOURCE_VFE_OUT;
		ife_out_res_tmp->is_dual_isp = in_port->usage_type;
		ife_out_res_tmp->res_id = out_port->res_type;
		ife_out_res_tmp->linked = true;

		ife_out_res->hw_res[0] = ife_out_res_tmp->hw_res[0];
		ife_out_res->is_dual_isp = 0;
		ife_out_res->use_wm_pack = ife_src_res->use_wm_pack;
		ife_out_res->res_type = CAM_ISP_RESOURCE_VFE_OUT;
		ife_out_res->is_dual_isp = in_port->usage_type;
		ife_out_res->res_id = out_port->res_type;
		ife_out_res->linked = true;

		ife_src_res->num_children++;
		c_ctx->num_acq_vfe_out++;
		if (out_port->secure_mode)
			ife_out_res->is_secure = true;
	}

	return 0;
err:
	/* release resource at the entry function */
	return rc;

}

static int cam_ife_hw_mgr_link_ife_out_resources(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	int  index)
{
	struct cam_isp_hw_mgr_res                *ife_src_res;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;
	int rc = -EINVAL;

	list_for_each_entry(ife_src_res, &c_ctx->res_list_ife_src, list) {
		if (ife_src_res->num_children)
			continue;

		switch (ife_src_res->res_id) {
		case CAM_ISP_HW_VFE_IN_CAMIF:
		case CAM_ISP_HW_VFE_IN_PDLIB:
		case CAM_ISP_HW_VFE_IN_RD:
		case CAM_ISP_HW_VFE_IN_LCR:
			rc = cam_ife_hw_mgr_link_res_ife_out_pixel(ife_ctx,
				ife_src_res, in_port, index);
			break;
		case CAM_ISP_HW_VFE_IN_RDI0:
		case CAM_ISP_HW_VFE_IN_RDI1:
		case CAM_ISP_HW_VFE_IN_RDI2:
		case CAM_ISP_HW_VFE_IN_RDI3:
		case CAM_ISP_HW_VFE_IN_RDI4:
		case CAM_ISP_HW_VFE_IN_RDI5:
			rc = cam_ife_hw_mgr_link_res_ife_out_rdi(ife_ctx,
				ife_src_res, in_port, index);
			break;
		default:
			CAM_ERR(CAM_ISP, "Unknown IFE SRC resource: %d",
				ife_src_res->res_id);
			break;
		}
		if (rc)
			goto err;
	}

	return 0;
err:
	/* release resource on entry function */
	return rc;
}

static int cam_ife_hw_mgr_link_hw_res(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	int    index, uint32_t *acquired_hw_id,
	uint32_t *acquired_hw_path,
	bool      crop_enable)
{
	int  rc;

	/* link csid resource pointer */
	if (list_empty(&g_ife_sns_grp_cfg.grp_cfg[index]->res_ife_csid_list)) {
		CAM_ERR(CAM_ISP, "No CSID resources available");
		return -EIO;
	}
	rc = cam_ife_hw_mgr_link_csid_resources(ife_ctx, in_port, crop_enable, index);
	if (rc)
		goto end;

	/* link ife_src  resource pointer */
	if (list_empty(&g_ife_sns_grp_cfg.grp_cfg[index]->res_ife_src_list)) {
		CAM_ERR(CAM_ISP, "No IFE_SRC resources available");
		return -EIO;
	}
	rc = cam_ife_hw_mgr_link_ife_src_resources(ife_ctx, in_port, index,
		acquired_hw_id, acquired_hw_path);
	if (rc)
		goto end;

	/* link ife_out  resource pointer */
	rc = cam_ife_hw_mgr_link_ife_out_resources(ife_ctx, in_port, index);
	if (rc)
		goto end;
end:
	return rc;

}

static int cam_ife_hw_mgr_update_vc_dt_pxl_path(
	struct cam_isp_hw_mgr_res      *isp_res,
	enum cam_ife_pix_path_res_id   path_res_id,
	int                            index,
	int                            stream_index,
	bool                           *found)
{
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg = g_ife_sns_grp_cfg.grp_cfg[index];

	switch (path_res_id) {
	case CAM_IFE_PIX_PATH_RES_IPP:
		if (grp_cfg->stream_cfg[stream_index].num_valid_vc_dt_pxl) {
			if (!grp_cfg->stream_cfg[stream_index].pxl_vc_dt_updated) {
				isp_res->vc =
					grp_cfg->stream_cfg[stream_index].pxl_vc;
				isp_res->dt =
					grp_cfg->stream_cfg[stream_index].pxl_dt;
				isp_res->decode_format =
					grp_cfg->stream_cfg[stream_index].decode_format;
				grp_cfg->stream_cfg[stream_index].pxl_vc_dt_updated =
					true;
				*found = true;
			} else {
				CAM_ERR(CAM_ISP, "No free pxl vc-dt available");
				return -EINVAL;
			}
		} else if (grp_cfg->stream_cfg[stream_index].num_valid_vc_dt_lcr) {
			if (!grp_cfg->stream_cfg[stream_index].lcr_vc_dt_updated) {
				isp_res->vc =
					grp_cfg->stream_cfg[stream_index].lcr_vc;
				isp_res->dt =
					grp_cfg->stream_cfg[stream_index].lcr_dt;
				isp_res->decode_format =
					grp_cfg->stream_cfg[stream_index].decode_format;
				grp_cfg->stream_cfg[stream_index].lcr_vc_dt_updated =
					true;
				*found = true;
			} else {
				CAM_ERR(CAM_ISP, "No free lcr vc-dt available");
				return -EINVAL;
			}
		}
		break;
	case CAM_IFE_PIX_PATH_RES_PPP:
		if (grp_cfg->stream_cfg[stream_index].num_valid_vc_dt_ppp) {
			if (!grp_cfg->stream_cfg[stream_index].ppp_vc_dt_updated) {
				isp_res->vc =
					grp_cfg->stream_cfg[stream_index].ppp_vc;
				isp_res->dt =
					grp_cfg->stream_cfg[stream_index].ppp_dt;
				isp_res->decode_format =
					grp_cfg->stream_cfg[stream_index].decode_format;
				grp_cfg->stream_cfg[stream_index].ppp_vc_dt_updated =
					true;
				*found = true;
			} else {
				CAM_ERR(CAM_ISP,
					"No free ppp vc-dt available");
				return -EINVAL;
			}
		}
		break;
	default:
		CAM_ERR(CAM_ISP, "Invlaid res_path_id:%d", path_res_id);
		return -EINVAL;
	}

	return 0;
}

int cam_ife_hw_mgr_update_vc_dt_stream_grp(
	struct cam_isp_hw_mgr_res           *isp_res,
	int                                  index,
	enum cam_ife_pix_path_res_id         path_res_id,
	bool                                 is_rdi_path)
{
	int i, j, rc;
	bool   found = false;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg;

	if (index != CAM_IFE_STREAM_GRP_INDEX_NONE) {
		grp_cfg = g_ife_sns_grp_cfg.grp_cfg[index];
		for (i = 0; i < grp_cfg->stream_cfg_cnt; i++) {
			if (is_rdi_path) {
				if ((path_res_id == CAM_IFE_PIX_PATH_RES_RDI_3 ||
					path_res_id == CAM_IFE_PIX_PATH_RES_RDI_5)) {
					if (grp_cfg->stream_cfg[i].color_filter_arrangement &&
						!grp_cfg->stream_cfg[i].yuv_rdi_vc_dt_updated) {
						for (j = grp_cfg->stream_cfg[i].rdi_vc_dt_updated;
							j < grp_cfg->stream_cfg[i].num_valid_vc_dt_rdi;
							j++) {
							isp_res->vc =
								grp_cfg->stream_cfg[i].rdi_vc[j];
							isp_res->dt =
								grp_cfg->stream_cfg[i].rdi_dt[j];
							isp_res->decode_format =
								grp_cfg->stream_cfg[i].decode_format;
							CAM_DBG(CAM_ISP,
								"[i:%d] [j:%d] vc: %d dt :%d decode_format :%d path_id: %d",
								i, j, isp_res->vc, isp_res->dt,
								isp_res->decode_format,
								path_res_id);
							grp_cfg->stream_cfg[i].rdi_vc_dt_updated++;
							grp_cfg->stream_cfg[i].yuv_rdi_vc_dt_updated =
								TRUE;
							found = true;
							break;
						}
					}

				} else {
					for (j = grp_cfg->stream_cfg[i].rdi_vc_dt_updated;
						j < (grp_cfg->stream_cfg[i].num_valid_vc_dt_rdi -
						grp_cfg->stream_cfg[i].color_filter_arrangement);
						j++) {
						isp_res->vc = grp_cfg->stream_cfg[i].rdi_vc[j];
						isp_res->dt = grp_cfg->stream_cfg[i].rdi_dt[j];
						isp_res->decode_format =
							grp_cfg->stream_cfg[i].decode_format;
						CAM_DBG(CAM_ISP,
							"[i:%d] [j:%d] vc: %d dt :%d decode_format :%d path_id: %d",
							i, j, isp_res->vc, isp_res->dt,
							isp_res->decode_format, path_res_id);
						grp_cfg->stream_cfg[i].rdi_vc_dt_updated++;
						found = true;
						break;
					}
				}
				if (found)
					break;
			} else {
				rc = cam_ife_hw_mgr_update_vc_dt_pxl_path(
					isp_res, path_res_id, index, i, &found);
				if (rc) {
					CAM_ERR(CAM_ISP,
						"couldnt update vc-dt index:%d path_id:%d",
						index, path_res_id);
					return rc;
				}
				if (found)
					break;
			}
		}

		if (i == grp_cfg->stream_cfg_cnt) {
			CAM_ERR(CAM_ISP,
				"no valid vc-dt available, stream_grp_index :%d", index);
			return -EINVAL;
		}
	} else {
		CAM_ERR(CAM_ISP, "invalid stream_grp_index :%d", index);
		return -EINVAL;
	}
	return 0;
}

static bool cam_ife_mgr_check_res_path_enabled(
	uint32_t     path_id,
	int          index)
{
	int i;
	bool res_path_enable = false;

	for (i = 0; i < g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg_cnt; i++) {
		switch (path_id) {
		case CAM_ISP_PXL_PATH:
			if (g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].num_valid_vc_dt_pxl) {
				res_path_enable = true;
				break;
			}
			break;
		case CAM_ISP_PPP_PATH:
			if (g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].num_valid_vc_dt_ppp) {
				res_path_enable = true;
				break;
			}
			break;
		case CAM_ISP_LCR_PATH:
			if (g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].num_valid_vc_dt_lcr) {
				res_path_enable = true;
				break;
			}
			break;
		}
		if (res_path_enable)
			break;
	}

	return res_path_enable;
}

static int cam_ife_hw_mgr_acquire_csid_res_stream_grp(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	bool                                 crop_enable,
	uint32_t                            *acquired_hw_path,
	int                                  index)
{
	int rc = 0;

	if (cam_ife_mgr_check_res_path_enabled(CAM_ISP_PXL_PATH, index) ||
		cam_ife_mgr_check_res_path_enabled(CAM_ISP_LCR_PATH, index)) {
		/* get ife CSID ipp resource */
		rc = cam_ife_hw_mgr_acquire_res_ife_csid_pxl(ife_ctx,
			in_port, true, crop_enable, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"Acquire IFE CSID IPP resource Failed");
			goto err;
		}
	}

	if (g_ife_sns_grp_cfg.grp_cfg[index]->rdi_stream_cfg_cnt) {
		rc = cam_ife_hw_mgr_acquire_res_ife_csid_rdi(ife_ctx, in_port,
			acquired_hw_path, index, true);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"Acquire IFE CSID RDI resource Failed");
			goto err;
		}
	}

	/* get ife csid PPP resource */
	/* If both IPP and PPP paths are requested with the same vc dt
	 * it is implied that the sensor is a type 3 PD sensor. Crop
	 * must be enabled for this sensor on PPP path as well.
	 */
	if (cam_ife_mgr_check_res_path_enabled(CAM_ISP_PPP_PATH, index)) {
		rc = cam_ife_hw_mgr_acquire_res_ife_csid_pxl(ife_ctx,
			in_port, false, crop_enable, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"Acquire IFE CSID PPP resource Failed");
			goto err;
		}
	}
err:
	return rc;
}

static int cam_ife_hw_mgr_acquire_ife_src_stream_grp(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	uint32_t                            *acquired_hw_id,
	uint32_t                            *acquired_hw_path,
	int                                  index)
{
	int rc = -EINVAL;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;

	if (cam_ife_mgr_check_res_path_enabled(CAM_ISP_PXL_PATH, index)) {
		rc = cam_ife_hw_mgr_acquire_res_ife_src(ife_ctx,
			in_port, false, false, false,
			acquired_hw_id, acquired_hw_path,
			CAM_IFE_PIX_PATH_RES_IPP, index);
		if (rc) {
			CAM_ERR(CAM_ISP,
				"Acquire IFE IPP SRC resource Failed");
			goto err;
		}
	}

	/* in case of rdi_res, pix path is sent as rdi0 always.
	 * This rdi0 path doesn't play any role for rdi res.
	 * Just for compilation purpose PIX_PATH_RES_RDI_0 is added for rdi res.
	 */
	if (g_ife_sns_grp_cfg.grp_cfg[index]->rdi_stream_cfg_cnt) {
		rc = cam_ife_hw_mgr_acquire_res_ife_src(ife_ctx,
			in_port, false, false, true,
			acquired_hw_id, acquired_hw_path,
			CAM_IFE_PIX_PATH_RES_RDI_0, index);

		if (rc) {
			CAM_ERR(CAM_ISP,
				"Acquire IFE RDI SRC resource Failed");
			goto err;
		}
	}

	if (cam_ife_mgr_check_res_path_enabled(CAM_ISP_LCR_PATH, index)) {
		rc = cam_ife_hw_mgr_acquire_res_ife_src(
			ife_ctx, in_port, true, false, false,
			acquired_hw_id, acquired_hw_path,
			CAM_IFE_PIX_PATH_RES_IPP, index);
		if (rc) {
			CAM_ERR(CAM_ISP, "Acquire IFE LCR SRC resource Failed");
			goto err;
		}
	}


	if (cam_ife_mgr_check_res_path_enabled(CAM_ISP_PPP_PATH, index)) {
		rc = cam_ife_hw_mgr_acquire_res_ife_src(ife_ctx, in_port,
				false, true, false,
				acquired_hw_id, acquired_hw_path,
				CAM_IFE_PIX_PATH_RES_PPP, index);
		if (rc) {
			CAM_ERR(CAM_ISP, "Acquire IFE PPP SRC resource Failed");
			goto err;
		}
	}

	return 0;

err:
	return rc;
}

int cam_ife_hw_mgr_acquire_res_stream_grp(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	uint32_t                            *acquired_hw_id,
	uint32_t                            *acquired_hw_path,
	uint32_t                            *acquired_rdi_res,
	bool                                 crop_enable)
{
	int rc = 0;
	bool found = false;
	int i, j;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ife_ctx->concr_ctx;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		for (j = 0; j < g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
			if (in_port->sensor_id ==
					g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].sensor_id) {
				mutex_lock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
				if (g_ife_sns_grp_cfg.grp_cfg[i]->acquire_cnt) {
					CAM_DBG(CAM_ISP,
						"hw devices are already acquired for sensor:0x%x ife-ctx:%d",
						in_port[i].sensor_id, c_ctx->ctx_index);

					/* assign resource pointers to ife hw ctx */
					rc = cam_ife_hw_mgr_link_hw_res(ife_ctx, in_port,
							i, acquired_hw_id, acquired_hw_path,
							crop_enable);
					if (rc) {
						CAM_ERR(CAM_ISP,
							"Can not link HW resources for sensor:0x%x ife-ctx:%d",
							in_port[i].sensor_id, c_ctx->ctx_index);
						goto end;
					}
				} else {
					/*acquire csid resources*/
					rc = cam_ife_hw_mgr_acquire_csid_res_stream_grp(ife_ctx,
						in_port, crop_enable, acquired_rdi_res, i);
					if (rc) {
						CAM_ERR(CAM_ISP,
							"Cannot acquire csid resources for sensor:0x%x ife-ctx:%d",
							in_port[i].sensor_id, c_ctx->ctx_index);
						goto end;
					}

					/*acquire ife_src resources*/
					rc = cam_ife_hw_mgr_acquire_ife_src_stream_grp(ife_ctx,
							in_port, acquired_hw_id,
							acquired_hw_path, i);
					if (rc) {
						CAM_ERR(CAM_ISP,
							"Cannot acquire ife_src resources for sensor:0x%x ife-ctx:%d",
							in_port[i].sensor_id, c_ctx->ctx_index);
						goto end;
					}
					/*acquire ife_out resources*/
					rc = cam_ife_hw_mgr_acquire_res_ife_out(ife_ctx,
						in_port, i);
					if (rc) {
						CAM_ERR(CAM_ISP,
							"Cannot acquire ife_out resources for sensor:0x%x ife-ctx:%d",
							in_port[i].sensor_id, c_ctx->ctx_index);
						goto end;
					}

					/* link resource pointers to ife hw ctx */
					rc = cam_ife_hw_mgr_link_hw_res(ife_ctx, in_port, i,
							acquired_hw_id, acquired_hw_path,
							crop_enable);
					if (rc) {
						CAM_ERR(CAM_ISP,
							"Can not link HW resources for sensor:0x%x ife-ctx:%d",
							in_port[i].sensor_id, c_ctx->ctx_index);
						goto end;
					}
					g_ife_sns_grp_cfg.grp_cfg[i]->acquired_hw_idx =
						*acquired_hw_id;
				}

				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].acquired = true;
				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].priv = ife_ctx;
				g_ife_sns_grp_cfg.grp_cfg[i]->acquire_cnt++;
				g_ife_sns_grp_cfg.grp_cfg[i]->hw_ctx_cnt++;
				mutex_unlock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);

				found = true;
				break;
			}
		}
		if (found)
			break;
	}
	return 0;
end:
	mutex_unlock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
	return rc;
}

int cam_ife_mgr_check_per_port_enable(
	struct cam_isp_in_port_generic_info *in_port)
{
	int i, j;
	bool   found = false;

	in_port->per_port_en = false;

	if (in_port->is_offline) {
		CAM_INFO(CAM_ISP, "NOT A PER PORT CTX");
		return 0;
	}

	if (!g_ife_sns_grp_cfg.num_grp_cfg)
		goto end;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		for (j = 0; j < g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
			if (in_port->sensor_id ==
				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].sensor_id) {
				in_port->per_port_en = true;
				found = true;
				CAM_DBG(CAM_ISP, "PER PORT ENABLE , sensor_id:%d per_port:%d",
					in_port->sensor_id, in_port->per_port_en);
				break;
			}
		}
		if (found)
			break;
	}

	if (!found)
		CAM_DBG(CAM_ISP, "PER PORT DISABLED, sensor_id:%d per_port:%d",
			in_port->sensor_id, in_port->per_port_en);
end:
	return 0;
}

static bool cam_ife_mgr_hw_validate_vc_dt_pxl_path(
	struct cam_ife_hw_mgr_sensor_stream_config  *stream_cfg,
	uint32_t vc, uint32_t dt)
{
	bool found = false;

	if (stream_cfg->num_valid_vc_dt_pxl) {
		if ((stream_cfg->pxl_vc == vc) &&
			(stream_cfg->pxl_dt == dt))
			found = true;
	} else if (stream_cfg->num_valid_vc_dt_ppp) {
		if ((stream_cfg->ppp_vc == vc) &&
			(stream_cfg->ppp_dt == dt))
			found = true;
	} else if (stream_cfg->num_valid_vc_dt_lcr) {
		if ((stream_cfg->lcr_vc == vc) &&
			(stream_cfg->lcr_dt == dt))
			found = true;
	} else {
		CAM_ERR(CAM_ISP, "valid vc-dt not found");
		found = false;
	}

	return found;
}

int cam_ife_mgr_hw_validate_vc_dt_stream_grp(
	struct cam_isp_in_port_generic_info *in_port,
	uint32_t vc, uint32_t dt)
{
	struct cam_ife_hw_mgr_sensor_stream_config  *stream_cfg;
	bool                                         found = false;
	int i, j, k, rc = 0;

	if (!in_port->per_port_en)
		return 0;

	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		for (j = 0; j < g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
			stream_cfg = &g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j];
			if (in_port->sensor_id == stream_cfg->sensor_id) {
				/*
				 * pxl vc-dt will match with one of the rdi vc-dt
				 * hence check wrt to rdi vc-dt
				 */
				for (k = 0; k < stream_cfg->num_valid_vc_dt_rdi; k++) {
					if ((stream_cfg->rdi_vc[k] == vc) &&
						(stream_cfg->rdi_dt[k] == dt)) {
						found = true;
						break;
					}
				}
				if (!found)
					found = cam_ife_mgr_hw_validate_vc_dt_pxl_path(stream_cfg,
						vc, dt);
			}
			if (found)
				break;
		}
		if (found)
			break;
	}

	if (!found) {
		CAM_ERR(CAM_ISP,
		"vc-dt[%d:%d] match not found in sensor stream group configurations",
			vc, dt);
		rc = -EINVAL;
	}

	return rc;
}

static int cam_ife_mgr_stop_hw_res_stream_grp(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	int                        grp_cfg_index,
	uint32_t                   stop_cmd,
	bool                       is_internal_stop)
{
	struct cam_isp_hw_mgr_res      *hw_mgr_res;
	struct cam_ife_hw_concrete_ctx *c_ctx = ife_ctx->concr_ctx;
	uint32_t                        i, master_base_idx = 0;

	/* get master base index first */
	for (i = 0; i < c_ctx->num_base; i++) {
		if (c_ctx->base[i].split_id == CAM_ISP_HW_SPLIT_LEFT) {
			master_base_idx = c_ctx->base[i].idx;
			break;
		}
	}

	/* stop csid resources */
	cam_ife_mgr_csid_stop_hw(ife_ctx,
		&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_csid_list,
		master_base_idx, stop_cmd);

	/* Ensure HW layer does not reset any clk data since it's
	 * internal stream off/resume
	 */
	if (is_internal_stop)
		cam_ife_mgr_finish_clk_bw_update(ife_ctx, 0, true);

	/* Ensure HW layer does not reset any clk data since it's
	 * internal stream off/resume
	 */
	if (is_internal_stop)
		cam_ife_mgr_finish_clk_bw_update(ife_ctx, 0, true);

	/* stop ife out resources */
	for (i = 0; i < max_ife_out_res; i++) {
		hw_mgr_res =
			&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_list_ife_out[i];

		/*hw_mgr_res can be NULL for virtual_rdi ports*/
		if (!hw_mgr_res)
			continue;

		cam_ife_hw_mgr_stop_hw_res(hw_mgr_res);
	}

	/* stop ife src resources */
	if (!list_empty(&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_src_list)) {
		list_for_each_entry(hw_mgr_res,
			&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_src_list,
			list) {
			cam_ife_hw_mgr_stop_hw_res(hw_mgr_res);
		}
	}
	return 0;
}

static int cam_ife_mgr_start_hw_res_stream_grp(
	int          grp_cfg_index,
	bool          is_internal_start,
	struct cam_ife_hw_mgr_ctx  *ife_ctx)
{
	int rc = 0;

	rc = cam_ife_hw_mgr_start_ife_out_res_stream_grp(grp_cfg_index, ife_ctx);
	if (rc) {
		CAM_ERR(CAM_ISP, "Can not start IFE OUT RES");
		goto end;
	}

	rc = cam_ife_hw_mgr_ife_src_start_hw_stream_grp(grp_cfg_index, ife_ctx);
	if (rc) {
		CAM_ERR(CAM_ISP, "Can not start IFE SRC RES");
		goto end;
	}

	/* Start the IFE CSID HW devices */
	rc = cam_ife_mgr_csid_start_hw_stream_grp(ife_ctx, grp_cfg_index, is_internal_start);
	if (rc) {
		CAM_ERR(CAM_ISP, "Can not start CSID RES");
		goto end;
	}
end:
	return rc;
}

int cam_ife_hw_mgr_res_stream_on_off_grp_cfg(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	void                      *hw_args,
	enum cam_ife_csid_halt_cmd csid_halt_type,
	bool                       is_start_hw,
	bool                      *per_port_feature_enable,
	bool                      *skip_hw_deinit)
{
	int i, j = 0, k = 0;
	int rc = -EINVAL;
	struct cam_ife_hw_mgr_stream_grp_config *grp_cfg = NULL;
	struct cam_ife_hw_concrete_ctx          *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_hw_mgr_res *hw_mgr_res = NULL;


	for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
		if (!g_ife_sns_grp_cfg.grp_cfg[i])
			continue;
		for (j = 0; j < g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
			if (c_ctx->sensor_id ==
				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].sensor_id) {
				grp_cfg = g_ife_sns_grp_cfg.grp_cfg[i];
				*per_port_feature_enable = true;
				break;
			}
		}
		if (grp_cfg)
			break;
	}

	if (!grp_cfg || !*per_port_feature_enable) {
		CAM_ERR(CAM_ISP, "cannot find stream config grp for sensor: %d ctx :%d",
			c_ctx->sensor_id, c_ctx->ctx_index);
		return rc;
	}

	mutex_lock(&grp_cfg->lock);

	if (is_start_hw) {
		struct cam_isp_start_args  *start_isp = hw_args;

		if (!grp_cfg->stream_on_cnt ||
			start_isp->start_only) {
			rc = cam_ife_mgr_start_hw_res_stream_grp(i,
				start_isp->is_internal_start, ife_ctx);
			if (rc) {
				CAM_ERR(CAM_ISP,
					"Can not start HW res stream grp");
				mutex_unlock(&grp_cfg->lock);
				goto err;
			}
		}
		if (start_isp->start_only) {
			rc =
			cam_ife_mgr_update_irq_mask_affected_ctx_stream_grp(
				ife_ctx, i, true,
				start_isp->is_internal_start);
		} else {
			rc = cam_ife_mgr_enable_irq(ife_ctx,
				start_isp->is_internal_start);
			if (rc) {
				CAM_ERR(CAM_ISP,
					"failed to enable irqs for ife_ctx: %d, sensor_id: 0x%x",
					c_ctx->ctx_index, c_ctx->sensor_id);
				mutex_unlock(&grp_cfg->lock);
				goto err;
			}

			grp_cfg->stream_cfg[j].is_streamon =
				true;
			grp_cfg->stream_on_cnt++;
		}
	} else {
		struct cam_isp_stop_args   *stop_isp = hw_args;
		*skip_hw_deinit = true;

		if (!grp_cfg->stream_cfg[j].is_streamon)
			rc = 0;

		if (stop_isp->stop_only) {
			rc =
			cam_ife_mgr_update_irq_mask_affected_ctx_stream_grp(
				ife_ctx, i, false, false);
		} else if (grp_cfg->stream_cfg[j].is_streamon) {
			rc = cam_ife_mgr_disable_irq(ife_ctx);
			if (rc) {
				CAM_WARN(CAM_ISP,
					"failed to disable irqs for ife_ctx: %d, sensor_id:0x%x",
					c_ctx->ctx_index, c_ctx->sensor_id);
			}
			grp_cfg->stream_cfg[j].is_streamon =
				false;

			if (grp_cfg->stream_on_cnt > 0)
				grp_cfg->stream_on_cnt--;
		}

		/* Stop IFE out resources */
		for (k = 0; k < c_ctx->num_acq_vfe_out; k++) {
			hw_mgr_res = &c_ctx->res_list_ife_out[k];
			if (hw_mgr_res && hw_mgr_res->hw_res[0])
				cam_ife_hw_mgr_stop_hw_res(hw_mgr_res);
		}

		if (grp_cfg->stream_on_cnt == 0) {
			if (!stop_isp->stop_only)
				*skip_hw_deinit = false;
			cam_ife_mgr_stop_hw_res_stream_grp(ife_ctx, i,
				csid_halt_type, stop_isp->is_internal_stop);
		}
	}
	mutex_unlock(&grp_cfg->lock);

err:
	return rc;
}

int cam_ife_hw_mgr_start_ife_out_res_stream_grp(
	int    grp_cfg_index,
	struct cam_ife_hw_mgr_ctx           *ife_ctx)
{
	int rc;
	struct cam_isp_hw_mgr_res           *hw_mgr_res;
	uint32_t i;

	/* Start all IFE out devices on first start call*/
	for (i = 0; i < max_ife_out_res; i++) {
		hw_mgr_res = &g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_list_ife_out[i];

		/*hw_mgr_res can be NULL for virtual_rdi ports*/
		if (!hw_mgr_res->hw_res[0])
			continue;

		hw_mgr_res->hw_res[0]->is_per_port_start = true;
		rc = cam_ife_hw_mgr_start_hw_res(hw_mgr_res, ife_ctx);
		if (rc) {
			CAM_ERR(CAM_ISP, "Can not start IFE OUT (%d) ", i);
			goto end;
		}
	}
end:
	return rc;
}

int cam_ife_hw_mgr_ife_src_start_hw_stream_grp(
	int                            grp_cfg_index,
	struct cam_ife_hw_mgr_ctx      *ife_ctx)
{
	int                                  rc = -1;
	struct cam_isp_hw_mgr_res           *hw_mgr_res;

	if (!list_empty(&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_src_list)) {
		list_for_each_entry(hw_mgr_res,
			&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_src_list, list) {
			hw_mgr_res->hw_res[0]->is_per_port_start = true;

			rc = cam_ife_hw_mgr_start_hw_res(hw_mgr_res, ife_ctx);
			if (rc) {
				CAM_ERR(CAM_ISP, "Can not start IFE Mux (%d)",
					 hw_mgr_res->res_id);
				goto err;
			}
		}
	}

err:
	return rc;
}

int cam_ife_mgr_csid_start_hw_stream_grp(
	struct cam_ife_hw_mgr_ctx       *ife_ctx,
	int           grp_cfg_index,
	bool          is_internal_start)
{
	struct cam_isp_hw_mgr_res      *hw_mgr_res;
	struct cam_isp_resource_node   *isp_res;
	struct cam_isp_resource_node   *res[CAM_IFE_PIX_PATH_RES_MAX - 1];
	struct cam_csid_hw_start_args  start_args;
	struct cam_hw_intf             *hw_intf;
	uint32_t  cnt;
	int rc = 0;

	/*check if any resources are not acquired, start them also */
	if (list_empty(&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_csid_list)) {
		CAM_ERR(CAM_ISP, "csid res list empty for grp_cfg_index:%d", grp_cfg_index);
		rc = -EINVAL;
	} else {
		cnt = 0;
		list_for_each_entry(hw_mgr_res,
			&g_ife_sns_grp_cfg.grp_cfg[grp_cfg_index]->res_ife_csid_list, list) {
			isp_res = hw_mgr_res->hw_res[0];

			if (!isp_res)
				return -EINVAL;

			isp_res->is_per_port_start = true;
			CAM_DBG(CAM_ISP, "csid[%u] res:%s res_id %d cnt %u",
				isp_res->hw_intf->hw_idx,
				isp_res->res_name, isp_res->res_id, cnt);
			res[cnt] = isp_res;
			cnt++;
		}

		if (cnt) {
			hw_intf =  res[0]->hw_intf;
			start_args.num_res = cnt;
			start_args.node_res = res;
			start_args.is_internal_start = is_internal_start;
			start_args.is_per_port_start = true;
			rc = hw_intf->hw_ops.start(hw_intf->hw_priv, &start_args,
					sizeof(start_args));
			if (rc)
				CAM_ERR(CAM_ISP, "Can not start CSID for non_acquired res");
		}
	}
	return rc;
}


static int cam_ife_mgr_update_vfe_irq_mask(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	bool                         is_vfe_src,
	bool                         enable_irq)
{
	struct cam_isp_hw_mgr_res      *hw_mgr_res;
	struct cam_isp_resource_node   *isp_res;
	struct cam_ife_hw_concrete_ctx *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_resource_node   *res[CAM_IFE_PIX_PATH_RES_MAX - 1];
	struct cam_hw_intf             *hw_intf = NULL;
	struct cam_vfe_res_irq_info     res_irq_mask;
	enum cam_isp_hw_cmd_type        cdm_type;
	uint32_t  cnt = 0;
	int i, rc = 0;

	if (is_vfe_src) {
		list_for_each_entry(hw_mgr_res, &c_ctx->res_list_ife_src, list) {
			isp_res = hw_mgr_res->hw_res[0];
			if (!isp_res) {
				CAM_ERR(CAM_ISP, "invalid param");
				goto end;
			}
			if (enable_irq)
				isp_res->is_per_port_start = false;

			CAM_DBG(CAM_ISP, "ife_src[%u] res:%s res_id %d cnt %u",
					isp_res->hw_intf->hw_idx,
					isp_res->res_name, isp_res->res_id, cnt);
			res[cnt] = isp_res;
			cnt++;
		}

		if (cnt) {
			hw_intf =  res[0]->hw_intf;
			res_irq_mask.num_res = cnt;
			res_irq_mask.node_res = res;
			res_irq_mask.priv = ife_ctx;
			res_irq_mask.enable_irq = enable_irq;

			cdm_type = CAM_ISP_HW_CMD_UPDATE_VFE_SRC_RES_IRQ_MASK;
		}
	} else {
		for (i = 0; i < c_ctx->num_acq_vfe_out; i++) {
			hw_mgr_res = &c_ctx->res_list_ife_out[i];
			isp_res = hw_mgr_res->hw_res[0];

			if (!isp_res)
				continue;

			if (enable_irq)
				isp_res->is_per_port_start = false;

			CAM_DBG(CAM_ISP, "ife_out[%u] res:%s res_id %d cnt %u",
					isp_res->hw_intf->hw_idx,
					isp_res->res_name, isp_res->res_id, cnt);
			res[cnt] = isp_res;
			cnt++;
		}

		if (cnt) {
			hw_intf =  res[0]->hw_intf;
			res_irq_mask.num_res = cnt;
			res_irq_mask.node_res = res;
			res_irq_mask.priv = ife_ctx;
			res_irq_mask.enable_irq = enable_irq;

			cdm_type = CAM_ISP_HW_CMD_UPDATE_VFE_OUT_RES_IRQ_MASK;
		}
	}

	if (hw_intf && hw_intf->hw_ops.process_cmd)
		rc = hw_intf->hw_ops.process_cmd(
			hw_intf->hw_priv,
			cdm_type,
			&res_irq_mask,
			sizeof(struct cam_vfe_res_irq_info));

	if (rc) {
		CAM_WARN(CAM_ISP, "%s %s hw res irq failed ctx: %d sensor_id: 0x%x",
			enable_irq ? "ENABLE" : "DISABLE",
			is_vfe_src ? "IFE_SRC" : "IFE_OUT",
			c_ctx->ctx_index, c_ctx->sensor_id);
	} else {
		CAM_DBG(CAM_ISP, "%s %s hw res irq success ctx: %d sensor_id: 0x%x",
			enable_irq ? "ENABLE" : "DISABLE",
			is_vfe_src ? "IFE_SRC" : "IFE_OUT",
			c_ctx->ctx_index, c_ctx->sensor_id);
	}

end:
	return rc;
}

static int cam_ife_mgr_update_csid_irq_mask(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	bool     is_internal_start,
	bool     enable_irq)
{
	struct cam_ife_hw_concrete_ctx *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_hw_mgr_res      *hw_mgr_res;
	struct cam_isp_resource_node   *isp_res;
	struct cam_isp_resource_node   *res[CAM_IFE_PIX_PATH_RES_MAX - 1];
	struct cam_csid_res_irq_info    res_irq_mask;
	struct cam_hw_intf             *hw_intf;
	uint32_t  cnt = 0;
	int rc = 0;


	list_for_each_entry(hw_mgr_res, &c_ctx->res_list_ife_csid, list) {
		isp_res = hw_mgr_res->hw_res[0];

		if (enable_irq)
			hw_mgr_res->hw_res[0]->is_per_port_start = false;

		CAM_DBG(CAM_ISP, "csid[%u] res:%s res_id %d cnt %u",
				isp_res->hw_intf->hw_idx,
				isp_res->res_name, isp_res->res_id, cnt);
		res[cnt] = isp_res;
		cnt++;
	}

	if (cnt) {
		hw_intf =  res[0]->hw_intf;
		res_irq_mask.num_res = cnt;
		res_irq_mask.node_res = res;
		res_irq_mask.priv = ife_ctx;
		res_irq_mask.enable_irq = enable_irq;
		res_irq_mask.is_internal_start = is_internal_start;

		if (hw_intf && hw_intf->hw_ops.process_cmd)
			rc = hw_intf->hw_ops.process_cmd(
				hw_intf->hw_priv,
				CAM_ISP_HW_CMD_UPDATE_CSID_RES_IRQ_MASK,
				&res_irq_mask,
				sizeof(struct cam_csid_res_irq_info));
	}
	return 0;
}

int cam_ife_mgr_disable_irq(
	struct cam_ife_hw_mgr_ctx           *ife_ctx)
{
	/*disable csid irqs*/
	cam_ife_mgr_update_csid_irq_mask(ife_ctx, false, false);

	/*disable ife_out irqs*/
	cam_ife_mgr_update_vfe_irq_mask(ife_ctx, false, false);

	/*disable ife src irqs*/
	cam_ife_mgr_update_vfe_irq_mask(ife_ctx, true, false);

	return 0;
}
int cam_ife_mgr_enable_irq(
	struct cam_ife_hw_mgr_ctx           *ife_ctx,
	bool                                 is_internal_start)
{
	struct cam_isp_hw_mgr_res           *hw_mgr_res = NULL;
	bool                                 res_rdi_context_set = false;
	uint32_t                             primary_rdi_out_res = 0;
	uint32_t                             primary_rdi_csid_res = 0;
	uint32_t                             primary_rdi_src_res = 0;
	struct cam_ife_hw_concrete_ctx       *c_ctx = ife_ctx->concr_ctx;
	struct cam_isp_resource_node         *isp_res;
	struct cam_isp_resource_node   *primary_rdi_res = NULL;
	uint32_t i;

	primary_rdi_src_res = CAM_ISP_HW_VFE_IN_MAX;
	primary_rdi_out_res = g_ife_hw_mgr.isp_caps.max_vfe_out_res_type;
	primary_rdi_csid_res = CAM_IFE_PIX_PATH_RES_MAX;

	for (i = 0; i < c_ctx->num_acq_vfe_out; i++) {
		hw_mgr_res = &c_ctx->res_list_ife_out[i];
		switch (hw_mgr_res->res_id) {
		case CAM_ISP_IFE_OUT_RES_RDI_0:
		case CAM_ISP_IFE_OUT_RES_RDI_1:
		case CAM_ISP_IFE_OUT_RES_RDI_2:
		case CAM_ISP_IFE_OUT_RES_RDI_3:
		case CAM_ISP_IFE_OUT_RES_RDI_4:
		case CAM_ISP_IFE_OUT_RES_RDI_5:
			if (!res_rdi_context_set && cam_isp_is_ctx_primary_rdi(ife_ctx)) {
				hw_mgr_res->hw_res[0]->is_rdi_primary_res =
					cam_isp_is_ctx_primary_rdi(ife_ctx);
				res_rdi_context_set = true;
				primary_rdi_out_res = hw_mgr_res->res_id;
			}
			break;
		default:
			break;
		}
	}

	/*enable ife_out irqs*/
	cam_ife_mgr_update_vfe_irq_mask(ife_ctx, false, true);

	if (primary_rdi_out_res < g_ife_hw_mgr.isp_caps.max_vfe_out_res_type) {
		primary_rdi_src_res =
			cam_convert_rdi_out_res_id_to_src(primary_rdi_out_res);
		primary_rdi_csid_res =
			cam_ife_hw_mgr_get_ife_csid_rdi_res_type(
			primary_rdi_out_res);
	}

	/* Start the IFE mux in devices */
	list_for_each_entry(hw_mgr_res, &c_ctx->res_list_ife_src, list) {
		if (primary_rdi_src_res == hw_mgr_res->res_id) {
			hw_mgr_res->hw_res[0]->is_rdi_primary_res =
			cam_isp_is_ctx_primary_rdi(ife_ctx);
		}
	}

	/*enable ife_src irqs*/
	cam_ife_mgr_update_vfe_irq_mask(ife_ctx, true, true);

	/*enable csid irqs*/
	list_for_each_entry(hw_mgr_res, &c_ctx->res_list_ife_csid, list) {
		isp_res = hw_mgr_res->hw_res[0];
		if (!isp_res)
			continue;
		if ((primary_rdi_csid_res == hw_mgr_res->res_id) ||
			(c_ctx->ctx_type == CAM_IFE_CTX_TYPE_SFE &&
			 isp_res->res_id == CAM_IFE_PIX_PATH_RES_RDI_0))
			primary_rdi_res = isp_res;

		CAM_DBG(CAM_ISP,
			"csid[%u] ctx_idx: %u res:%s res_id %d",
			isp_res->hw_intf->hw_idx, c_ctx->ctx_index,
			isp_res->res_name, isp_res->res_id);
	}

	if (!(hw_mgr_res->res_id == CAM_IFE_PIX_PATH_RES_IPP) && primary_rdi_res)
		primary_rdi_res->is_rdi_primary_res = true;

	/*enable csid irqs*/
	cam_ife_mgr_update_csid_irq_mask(ife_ctx, is_internal_start, true);

	return 0;
}

int cam_ife_mgr_update_irq_mask_affected_ctx_stream_grp(
	struct cam_ife_hw_mgr_ctx           *ctx,
	int                                 index,
	bool                                enable_irq,
	bool                                is_internal_start)
{
	int i, rc = 0;
	struct cam_ife_hw_mgr_ctx               *ife_ctx;
	struct cam_ife_hw_concrete_ctx           *c_ctx = ctx->concr_ctx;

	for (i = 0; i < g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg_cnt; i++) {
		if (g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].acquired &&
			g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].priv !=
			NULL) {
			ife_ctx =
				g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].priv;

			if (c_ctx->ctx_index != ife_ctx->concr_ctx->ctx_index)
				continue;

			if (enable_irq) {
				rc = cam_ife_mgr_enable_irq(ife_ctx, is_internal_start);
				if (rc) {
					CAM_ERR(CAM_ISP,
						"Failed to enable irq for ctx: %u sensor_id: 0x%x",
						ife_ctx->concr_ctx->ctx_index,
						ife_ctx->concr_ctx->sensor_id);
					goto end;
				}
				g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].is_streamon =
					true;
				g_ife_sns_grp_cfg.grp_cfg[index]->stream_on_cnt++;
			} else {
				rc = cam_ife_mgr_disable_irq(ife_ctx);
				if (rc) {
					CAM_ERR(CAM_ISP,
						"Failed to disable irq for ctx: %u sensor_id: 0x%x",
						ife_ctx->concr_ctx->ctx_index,
						ife_ctx->concr_ctx->sensor_id);
					goto end;
				}
				g_ife_sns_grp_cfg.grp_cfg[index]->stream_cfg[i].is_streamon =
					false;

				if (g_ife_sns_grp_cfg.grp_cfg[index]->stream_on_cnt > 0)
					g_ife_sns_grp_cfg.grp_cfg[index]->stream_on_cnt--;
			}
		}
	}
end:
	return rc;
}

int cam_ife_hw_mgr_free_hw_ctx(
	struct cam_ife_hw_mgr_ctx        *ife_ctx)
{
	uint32_t                          i, j;
	struct cam_isp_hw_mgr_res        *hw_mgr_res;
	struct cam_isp_hw_mgr_res        *hw_mgr_res_temp;
	struct cam_ife_hw_mgr_stream_grp_config *grp_cfg;
	bool                              found = false;
	struct cam_ife_hw_concrete_ctx   *c_ctx = ife_ctx->concr_ctx;

	/* ife leaf resource */
	for (i = 0; i < c_ctx->num_acq_vfe_out; i++) {
		/* caller should make sure the resource is in a list */
		hw_mgr_res = &c_ctx->res_list_ife_out[i];

		if (!hw_mgr_res->hw_res[0])
			continue;
		hw_mgr_res->linked = false;
		memset(hw_mgr_res, 0, sizeof(*hw_mgr_res));
		c_ctx->num_acq_vfe_out--;
	}

	/* fetch rd resource */
	list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_temp,
		&c_ctx->res_list_ife_in_rd, list) {
		hw_mgr_res->linked = false;
		list_del_init(&hw_mgr_res->list);
		memset(hw_mgr_res, 0, sizeof(*hw_mgr_res));
		INIT_LIST_HEAD(&hw_mgr_res->list);

		cam_ife_hw_mgr_put_res(&c_ctx->free_res_list, &hw_mgr_res);
	}

	/* ife source resource */
	list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_temp,
		&c_ctx->res_list_ife_src, list) {
		hw_mgr_res->linked = false;
		list_del_init(&hw_mgr_res->list);
		memset(hw_mgr_res, 0, sizeof(*hw_mgr_res));
		INIT_LIST_HEAD(&hw_mgr_res->list);

		cam_ife_hw_mgr_put_res(&c_ctx->free_res_list, &hw_mgr_res);
	}

	/* ife csid resource */
	list_for_each_entry_safe(hw_mgr_res, hw_mgr_res_temp,
		&c_ctx->res_list_ife_csid, list) {
		hw_mgr_res->linked = false;
		list_del_init(&hw_mgr_res->list);
		memset(hw_mgr_res, 0, sizeof(*hw_mgr_res));
		INIT_LIST_HEAD(&hw_mgr_res->list);

		cam_ife_hw_mgr_put_res(&c_ctx->free_res_list, &hw_mgr_res);
	}

	/* ife root node */
	if (c_ctx->res_list_ife_in.res_type != CAM_ISP_RESOURCE_UNINT)
		cam_ife_hw_mgr_free_hw_res(&c_ctx->res_list_ife_in, true);

	/* clean up the callback function */
	c_ctx->common.cb_priv = NULL;
	c_ctx->common.event_cb = NULL;

	c_ctx->flags.need_csid_top_cfg = false;

	if (c_ctx->flags.per_port_en) {
		for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
			if (!g_ife_sns_grp_cfg.grp_cfg[i])
				continue;
			grp_cfg = g_ife_sns_grp_cfg.grp_cfg[i];
			for (j = 0; j < grp_cfg->stream_cfg_cnt; j++) {
				if (grp_cfg->stream_cfg[j].sensor_id ==
					c_ctx->sensor_id) {
					grp_cfg->stream_cfg[j].acquired = false;
					grp_cfg->stream_cfg[j].priv = NULL;
					grp_cfg->stream_cfg[j].lcr_vc_dt_updated = false;
					grp_cfg->stream_cfg[j].pxl_vc_dt_updated = false;
					grp_cfg->stream_cfg[j].ppp_vc_dt_updated = false;
					if (grp_cfg->stream_cfg[j].rdi_vc_dt_updated > 0)
						grp_cfg->stream_cfg[j].rdi_vc_dt_updated--;
					if (grp_cfg->acquire_cnt > 0)
						grp_cfg->acquire_cnt--;
					found = true;
					break;
				}
			}
			if (found)
				break;
		}
	}

	CAM_DBG(CAM_ISP, "release context completed ctx id:%d",
		c_ctx->ctx_index);

	return 0;
}

int cam_ife_mgr_get_active_hw_ctx_cnt(
	struct cam_ife_hw_mgr_ctx  *ife_ctx,
	struct cam_isp_hw_cmd_args *isp_hw_cmd_args)
{
	int i, j, rc = 0;
	bool found = false;
	struct cam_ife_hw_concrete_ctx  *c_ctx = ife_ctx->concr_ctx;

	if (c_ctx->flags.per_port_en && !c_ctx->flags.is_dual) {
		for (i = 0; i < CAM_ISP_STREAM_GROUP_CFG_MAX; i++) {
			if (!g_ife_sns_grp_cfg.grp_cfg[i])
				continue;
			for (j = 0; j <
				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
				if (c_ctx->sensor_id ==
					g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].sensor_id) {
					found = true;
					break;
				}
			}
			if (found)
				break;
		}

		if (i == CAM_ISP_STREAM_GROUP_CFG_MAX) {
			isp_hw_cmd_args->u.active_hw_ctx.hw_ctx_cnt = 0;
			isp_hw_cmd_args->u.active_hw_ctx.stream_grp_cfg_index = -1;
			CAM_ERR(CAM_ISP, "cannot find stream grp cfg for ctx:%d sensor_id:%d",
				c_ctx->ctx_index, c_ctx->sensor_id);
			rc = -EINVAL;
			goto end;
		} else {
			mutex_lock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
			isp_hw_cmd_args->u.active_hw_ctx.hw_ctx_cnt =
				g_ife_sns_grp_cfg.grp_cfg[i]->acquire_cnt;
			isp_hw_cmd_args->u.active_hw_ctx.stream_grp_cfg_index = i;
			mutex_unlock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
		}
	}

	CAM_DBG(CAM_ISP, "ctx :%u sensor_id: 0x%x hw_ctx_cnt %d grp_cfg_index :%d",
		c_ctx->ctx_index, c_ctx->sensor_id,
		isp_hw_cmd_args->u.active_hw_ctx.hw_ctx_cnt,
		isp_hw_cmd_args->u.active_hw_ctx.stream_grp_cfg_index);

end:
	return rc;
}

int cam_ife_mgr_get_active_hw_ctx(
	struct cam_ife_hw_mgr_ctx           *ctx,
	struct cam_isp_hw_cmd_args *isp_hw_cmd_args)
{
	struct cam_ife_hw_mgr_ctx        *ife_ctx;
	struct cam_isp_hw_active_hw_ctx  *active_hw_ctx_info;
	struct cam_ife_hw_concrete_ctx   *c_ctx = ctx->concr_ctx;
	int i, j;

	active_hw_ctx_info = (struct cam_isp_hw_active_hw_ctx  *)isp_hw_cmd_args->cmd_data;
	i = active_hw_ctx_info->stream_grp_cfg_index;

	if (c_ctx->flags.per_port_en && !c_ctx->flags.is_dual) {
		mutex_lock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
		for (j = active_hw_ctx_info->index; j <
			g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg_cnt; j++) {
			if (g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].acquired &&
				g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].priv != NULL) {
				ife_ctx =
					g_ife_sns_grp_cfg.grp_cfg[i]->stream_cfg[j].priv;
				isp_hw_cmd_args->u.ptr = (void *)ife_ctx->cb_priv;
				active_hw_ctx_info->index = ++j;
				break;
			}
		}
		mutex_unlock(&g_ife_sns_grp_cfg.grp_cfg[i]->lock);
	}

	CAM_DBG(CAM_ISP,
		"ctx :%u sensor_id: 0x%x grp_cfg_index :%d next_active_hw_index: %d",
		c_ctx->ctx_index, c_ctx->sensor_id,
		active_hw_ctx_info->stream_grp_cfg_index,
		active_hw_ctx_info->index);

	return 0;
}

