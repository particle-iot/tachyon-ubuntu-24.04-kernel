/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CAM_IFE_HW_MGR_H_
#define _CAM_IFE_HW_MGR_H_

#include <linux/completion.h>
#include <linux/time.h>
#include "cam_isp_hw_mgr.h"
#include "cam_vfe_hw_intf.h"
#include "cam_ife_csid_hw_intf.h"
#include "cam_tasklet_util.h"
#include "cam_cdm_intf_api.h"
#include "cam_cpas_api.h"

/*
 * enum cam_ife_ctx_master_type - HW master type
 * CAM_IFE_CTX_TYPE_NONE: IFE ctx/stream directly connected to CSID
 * CAM_IFE_CTX_TYPE_CUSTOM: IFE ctx/stream connected to custom HW
 * CAM_IFE_CTX_TYPE_SFE: IFE ctx/stream connected to SFE
 */
enum cam_ife_ctx_master_type {
	CAM_IFE_CTX_TYPE_NONE,
	CAM_IFE_CTX_TYPE_CUSTOM,
	CAM_IFE_CTX_TYPE_SFE,
	CAM_IFE_CTX_TYPE_MAX,
};

/* IFE resource constants */
#define CAM_IFE_HW_IN_RES_MAX                 (CAM_ISP_IFE_IN_RES_MAX & 0xFF)
#define CAM_IFE_HW_RES_POOL_MAX               64
#define CAM_IFE_HW_STREAM_GRP_RES_POOL_MAX    32

/* IFE_HW_MGR ctx config */
#define CAM_IFE_CTX_CFG_FRAME_HEADER_TS   BIT(0)
#define CAM_IFE_CTX_CFG_SW_SYNC_ON        BIT(1)
#define CAM_IFE_CTX_CFG_DYNAMIC_SWITCH_ON BIT(2)

/* Maximum set for irq injection*/
#define MAX_INJECT_SET 10

/**
 * struct cam_ife_hw_mgr_debug - contain the debug information
 *
 * @dentry:                    Debugfs entry
 * @csid_debug:                csid debug information
 * @rx_capture_debug:          rx capture debug info
 * @enable_recovery:           enable recovery
 * @camif_debug:               camif debug info
 * @enable_csid_recovery:      enable csid recovery
 * @sfe_debug:                 sfe debug config
 * @sfe_sensor_diag_cfg:       sfe sensor diag config
 * @csid_test_bus:             csid test bus config
 * @sfe_cache_debug:           sfe cache debug info
 * @ife_perf_counter_val:      ife perf counter values
 * @sfe_perf_counter_val:      sfe perf counter values
 * @enable_req_dump:           Enable request dump on HW errors
 * @per_req_reg_dump:          Enable per request reg dump
 * @disable_ubwc_comp:         Disable UBWC compression
 * @disable_ife_mmu_prefetch:  Disable MMU prefetch for IFE bus WR
 * @enable_ife_frame_irqs:     Enable Frame timing IRQs for IFE/MCTFE
 * @rx_capture_debug_set:      If rx capture debug is set by user
 * @disable_isp_drv:           Disable ISP DRV config
 * @enable_presil_reg_dump:    Enable per req regdump in presil
 * @enable_cdm_cmd_check:      Enable invalid command check in cmd_buf
 * @disable_line_based_mode:   Disable line based mode for per port
 *                             feature with duplicate sensors
 */
struct cam_ife_hw_mgr_debug {
	struct dentry  *dentry;
	uint64_t       csid_debug;
	uint32_t       rx_capture_debug;
	uint32_t       enable_recovery;
	uint32_t       camif_debug;
	uint32_t       enable_csid_recovery;
	uint32_t       sfe_debug;
	uint32_t       sfe_sensor_diag_cfg;
	uint32_t       csid_test_bus;
	uint32_t       sfe_cache_debug[CAM_SFE_HW_NUM_MAX];
	uint32_t      *ife_perf_counter_val;
	uint32_t      *sfe_perf_counter_val;
	bool           enable_req_dump;
	bool           per_req_reg_dump;
	bool           disable_ubwc_comp;
	bool           disable_ife_mmu_prefetch;
	bool           enable_ife_frame_irqs;
	bool           rx_capture_debug_set;
	bool           disable_isp_drv;
	bool           enable_presil_reg_dump;
	bool           enable_cdm_cmd_check;
	bool           disable_line_based_mode;
};

/**
 * struct cam_ife_hw_mgr_ctx_pf_info - pf buf info
 *
 * @out_port_id: Out port id
 * @mid: MID value
 */
struct cam_ife_hw_mgr_ctx_pf_info {
	uint32_t       out_port_id;
	uint32_t       mid;
};

/**
 * struct cam_ife_sfe_scratch_buf_info - Scratch buf info
 *
 * @width: Width in pixels
 * @height: Height in pixels
 * @stride: Stride in pixels
 * @slice_height: Height in lines
 * @io_addr: Buffer address
 * @res_id: Resource type
 * @offset: Buffer offset
 * @config_done: To indicate if RDIx received scratch cfg
 * @is_secure: secure scratch buffer
 */
struct cam_ife_sfe_scratch_buf_info {
	uint32_t   width;
	uint32_t   height;
	uint32_t   stride;
	uint32_t   slice_height;
	dma_addr_t io_addr;
	uint32_t   res_id;
	uint32_t   offset;
	bool       config_done;
	bool       is_secure;
};

/**
 * struct cam_sfe_scratch_buf_cfg - Scratch buf info
 *
 * @num_configs: Total Number of scratch buffers provided
 * @streamon_buf_mask: Mask to indicate which ports have received buffer
 *                     from userspace, apply scratch for other ports
 * @updated_num_exp: Current num of exposures
 * @buf_info: Info on each of the buffers
 * @skip_scratch_cfg_streamon: Determine if scratch cfg needs to be programmed at stream on
 *
 */
struct cam_sfe_scratch_buf_cfg {
	uint32_t                            num_config;
	uint32_t                            streamon_buf_mask;
	uint32_t                            updated_num_exp;
	struct cam_ife_sfe_scratch_buf_info buf_info[
		CAM_SFE_FE_RDI_NUM_MAX];
	bool                                skip_scratch_cfg_streamon;
};

/**
 * struct cam_ife_scratch_buf_cfg - Scratch buf info
 *
 * @num_config: Total Number of scratch buffers provided
 * @streamon_buf_mask: Mask to indicate which ports have received buffer
 *                     from userspace, apply scratch for other ports
 * @buf_info: Info on each of the buffers
 * @skip_scratch_cfg_streamon: Determine if scratch cfg needs to be programmed at stream on
 *
 */
struct cam_ife_scratch_buf_cfg {
	uint32_t                            num_config;
	uint32_t                            streamon_buf_mask;
	struct cam_ife_sfe_scratch_buf_info buf_info[
		CAM_IFE_SCRATCH_NUM_MAX];
	bool                                skip_scratch_cfg_streamon;
};

/**
 * struct cam_ife_hw_mgr_ctx_scratch_buf_info - Scratch buffer info
 *
 * @num_fetches:               Indicate number of SFE fetches for this stream
 * @sfe_scratch_config:        Scratch buffer config if any for SFE ports
 * @ife_scratch_config:        Scratch buffer config if any for IFE ports
 */
struct cam_ife_hw_mgr_ctx_scratch_buf_info {
	uint32_t                        num_fetches;
	struct cam_sfe_scratch_buf_cfg *sfe_scratch_config;
	struct cam_ife_scratch_buf_cfg *ife_scratch_config;
};

/**
 * struct cam_ife_hw_mgr_ctx_flags - IFE HW mgr ctx flags
 *
 * @ctx_in_use:          flag to tell whether context is active
 * @init_done:           indicate whether init hw is done
 * @is_fe_enabled:       indicate whether fetch engine\read path is enabled
 * @is_dual:             indicate whether context is in dual VFE mode
 * @is_offline:          indicate whether context is for offline IFE
 * @dsp_enabled:         indicate whether dsp is enabled in this context
 * @internal_cdm:        indicate whether context uses internal CDM
 * @pf_mid_found:        in page fault, mid found for this ctx.
 * @need_csid_top_cfg:   Flag to indicate if CSID top cfg is needed.
 * @is_rdi_only_context: flag to specify the context has only rdi resource
 * @is_lite_context:     flag to specify the context has only uses lite
 *                       resources
 * @is_sfe_shdr:         indicate if stream is for SFE sHDR
 * @is_sfe_fs:           indicate if stream is for inline SFE FS
 * @dump_on_flush:       Set if reg dump triggered on flush
 * @dump_on_error:       Set if reg dump triggered on error
 * @custom_aeb_mode:     Set if custom AEB stream
 * @rdi_lcr_en:          To indicate if RDI LCR is enabled
 * @sys_cache_usage:     Per context sys cache usage
 *                       The corresponding index will be set
 *                       for the cache type
 * @rdi_pd_context:      Flag to specify the context has
 *                       only rdi and PD resource without PIX port.
 * @per_port_en:         Indicates if per port feature is enabled or not

 */
struct cam_ife_hw_mgr_ctx_flags {
	bool   ctx_in_use;
	bool   init_done;
	bool   is_fe_enabled;
	bool   is_dual;
	bool   is_offline;
	bool   dsp_enabled;
	bool   internal_cdm;
	bool   pf_mid_found;
	bool   need_csid_top_cfg;
	bool   is_rdi_only_context;
	bool   is_lite_context;
	bool   is_sfe_shdr;
	bool   is_sfe_fs;
	bool   dump_on_flush;
	bool   dump_on_error;
	bool   is_aeb_mode;
	bool   rdi_lcr_en;
	bool   sys_cache_usage[CAM_LLCC_MAX];
	bool   rdi_pd_context;
	bool   per_port_en;
};

/**
 * struct cam_ife_cdm_user_data - IFE HW user data with CDM
 *
 * @prepare:                   hw_update_data
 * @request_id:                Request id
 */
struct cam_ife_cdm_user_data {
	struct cam_isp_prepare_hw_update_data    *hw_update_data;
	uint64_t                                  request_id;
};

/**
 * struct cam_ife_mgr_bw_data - contain data to calc bandwidth for context
 *
 * @format:                    image format
 * @width:                     image width
 * @height:                    image height
 * @framerate:                 framerate
 *
 */
struct cam_ife_mgr_bw_data {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t framerate;
};

/* struct cam_ife_hw_mgr_ctx - IFE HW manager Context object
 *
 * concr_ctx:             HW Context currently used from this manager context
 * hw_mgr:                IFE hw mgr which owns this context
 * event_cb:              event callbacks
 * cb_priv:               event callbacks data
 * ctx_in_use:            indicates if context is active
 * ctx_idx:               index of this context
 * bw_data:               contains data for BW usage calculation
 * num_in_ports:          number of context input ports
 * in_ports:              context input ports
 * unpacker_fmt:          IFE input unpacker for offline isp
 * is_offline:            indicates if context is used for offline processing
 * stop_done_complete:    completion signaled when context is ceased operation
 * is_stopping:           if context is about to cease operation
 * @virtual_rdi_mapping_cb Callback query for virtual rdi mapping
 *
 */
struct cam_ife_hw_mgr_ctx {
	struct cam_ife_hw_concrete_ctx         *concr_ctx;
	struct cam_ife_hw_mgr                  *hw_mgr;
	cam_hw_event_cb_func                    event_cb[CAM_ISP_HW_EVENT_MAX];
	void                                   *cb_priv;
	uint32_t                                ctx_in_use;
	uint32_t                                ctx_idx;
	struct cam_ife_mgr_bw_data              bw_data;
	uint32_t                                num_in_ports;
	struct cam_isp_in_port_generic_info    *in_ports;
	uint32_t                                unpacker_fmt;
	bool                                    is_offline;
	cam_hw_get_virtual_rdi_mapping_cb_func  virtual_rdi_mapping_cb;
};


/**
 * struct cam_isp_context_comp_record:
 *
 * @brief:              Structure record the res id reserved on a comp group
 *
 * @num_res:            Number of valid resource IDs in this record
 * @res_id:             Resource IDs to report buf dones
 * @last_consumed_addr: Last consumed addr for resource ID at that index
 *
 */
struct cam_isp_context_comp_record {
	uint32_t num_res;
	uint32_t res_id[CAM_NUM_OUT_PER_COMP_IRQ_MAX];
};

/**
 * struct cam_isp_comp_record_query:
 *
 * @brief:              Structure record the bus comp group pointer information
 *
 * @vfe_bus_comp_grp:   Vfe bus comp group pointer
 * @vfe_bus_comp_grp:   Sfe bus comp group pointer
 *
 */
struct cam_isp_comp_record_query {
	struct cam_isp_context_comp_record        *vfe_bus_comp_grp;
	struct cam_isp_context_comp_record        *sfe_bus_comp_grp;
};

/** struct cam_ife_virtual_rdi_mapping - mapping table between UMd and KMD RDI resources
 *
 * @rdi_path_count           : indicates how many rdi paths are acquired for this sensor
 * @virtual_rdi              : requested virtual RDI port by UMD
 * @acquired_rdi             : acquired RDI port by KMD
 */
struct cam_ife_virtual_rdi_mapping {
	uint32_t   rdi_path_count;
	uint32_t   virtual_rdi[CAM_ISP_STREAM_CFG_MAX];
	uint32_t   acquired_rdi[CAM_ISP_STREAM_CFG_MAX];
};

/**
 * struct cam_ife_hw_concrete_ctx - IFE HW Context object that contains
 *                                  HW specific data
 *
 * @list:                   used by the ctx list.
 * @common:                 common acquired context data
 * @sensor_id:              Sensor id for context
 * @ctx_index:              acquired context id.
 * @left_hw_idx:            hw index for master core [left]
 * @right_hw_idx:           hw index for slave core [right]
 * @acquired_hw_id:         hw id that this context acquired
 * @served_ctx_id:          contains currently and next served context ids
 * @served_ctx_r:           index pointing to currently served context id
 * @served_ctx_w:           index pointing to next served context id
 * @hw_mgr:                 IFE hw mgr which owns this context
 * @res_list_csid:          CSID resource list
 * @res_list_ife_src:       IFE input resource list
 * @res_list_sfe_src        SFE input resource list
 * @res_list_ife_in_rd      IFE/SFE input resource list for read path
 * @res_list_ife_out:       IFE output resoruces array
 * @res_list_sfe_out:       SFE output resources array
 * @vfe_out_map:            Map for VFE out ports
 * @sfe_out_map:            Map for SFE out ports
 * @num_acq_vfe_out:        Number of acquired VFE out resources
 * @num_acq_sfe_out:        Number of acquired SFE out resources
 * @free_res_list:          Free resources list for the branch node
 * @res_pool:               memory storage for the free resource list
 * @irq_status0_mask:       irq_status0_mask for the context
 * @irq_status1_mask:       irq_status1_mask for the context
 * @base                    device base index array contain the all IFE HW
 *                          instance associated with this context.
 * @num_base                number of valid base data in the base array
 * @cdm_handle              cdm hw acquire handle
 * @cdm_hw_idx:             Physical CDM in use
 * @cdm_ops                 cdm util operation pointer for building
 *                          cdm commands
 * @cdm_cmd                 cdm base and length request pointer
 * @cdm_id                  cdm id of the acquired cdm
 * @sof_cnt                 sof count value per core, used for dual VFE
 * @epoch_cnt               epoch count value per core, used for dual VFE
 * @eof_cnt                 eof count value per core, used for dual VFE
 * @overflow_pending        flat to specify the overflow is pending for the
 *                          context
 * @cdm_done                flag to indicate cdm has finished writing shadow
 *                          registers
 * @last_cdm_done_req:      Last cdm done request
 * @config_done_complete    indicator for configuration complete
 * @reg_dump_buf_desc:      cmd buffer descriptors for reg dump
 * @num_reg_dump_buf:       Count of descriptors in reg_dump_buf_desc
 * @applied_req_id:         Last request id to be applied
 * @ctx_type                Type of IFE ctx [CUSTOM/SFE etc.]
 * @ctx_config              ife ctx config  [bit field]
 * @ts                      captured timestamp when the ctx is acquired
 * @hw_enabled              Array to indicate active HW
 * @buf_done_controller     Buf done controller.
 * @scratch_buf_info        Scratch buf [SFE/IFE] info pertaining to this stream
 * @flags                   Flags pertainting to this ctx
 * @bw_config_version       BW Config version
 * @recovery_id:            Unique ID of the current valid scheduled recovery
 * @current_mup:            Current MUP val, scratch will then apply the same as previously
 *                          applied request
 * @curr_num_exp:           Current num of exposures
 * @try_recovery_cnt:       Retry count for overflow recovery
 * @recovery_req_id:        The request id on which overflow recovery happens
 * @drv_path_idle_en:       Path idle enable value for DRV
 * @major_version:          Major version for acquire
 * @vfe_bus_comp_grp:       VFE composite group placeholder
 * @sfe_bus_comp_grp:       SFE composite group placeholder
 * @cdm_done_ts:            CDM callback done timestamp
 * @is_hw_ctx_acq:          If acquire for ife ctx is having hw ctx acquired
 * @acq_hw_ctxt_src_dst_map: Src to dst hw ctxt map for acquired pixel paths
 * @ctx_state               Indicates context state
 * @offline_clk             IFE Clock value to be configured for offline processing
 * @offline_sfe_clk         SFE Clock value to be configured for offline processing
 * @mapping_table:          mapping between virtual rdi and acquired rdi
 *
 */

struct cam_ife_hw_concrete_ctx {
	struct list_head                     list;
	struct cam_isp_hw_mgr_ctx            common;
	void                                *tasklet_info;
	uint32_t                             sensor_id;
	uint32_t                             ctx_index;
	uint32_t                             left_hw_idx;
	uint32_t                             right_hw_idx;
	uint32_t                             acquired_hw_id;
	uint32_t                             served_ctx_id[2];
	uint32_t                             served_ctx_r;
	uint32_t                             served_ctx_w;
	struct cam_ife_hw_mgr               *hw_mgr;

	struct cam_isp_hw_mgr_res            res_list_ife_in;
	struct list_head                     res_list_ife_csid;
	struct list_head                     res_list_ife_src;
	struct list_head                     res_list_sfe_src;
	struct list_head                     res_list_ife_in_rd;
	struct cam_isp_hw_mgr_res           *res_list_ife_out;
	struct cam_isp_hw_mgr_res           *res_list_sfe_out;
	struct list_head                     free_res_list;
	struct cam_isp_hw_mgr_res            res_pool[CAM_IFE_HW_RES_POOL_MAX];
	uint8_t                              *vfe_out_map;
	uint8_t                              *sfe_out_map;
	uint32_t                             num_acq_vfe_out;
	uint32_t                             num_acq_sfe_out;

	uint32_t                             irq_status0_mask[CAM_IFE_HW_NUM_MAX];
	uint32_t                             irq_status1_mask[CAM_IFE_HW_NUM_MAX];
	struct cam_isp_ctx_base_info         base[CAM_IFE_HW_NUM_MAX +
							CAM_SFE_HW_NUM_MAX];
	uint32_t                             num_base;
	uint32_t                             cdm_handle;
	int32_t                              cdm_hw_idx;
	struct cam_cdm_utils_ops            *cdm_ops;
	struct cam_cdm_bl_request           *cdm_cmd;
	enum cam_cdm_id                      cdm_id;
	uint32_t                             sof_cnt[CAM_IFE_HW_NUM_MAX];
	uint32_t                             epoch_cnt[CAM_IFE_HW_NUM_MAX];
	uint32_t                             eof_cnt[CAM_IFE_HW_NUM_MAX];
	atomic_t                             overflow_pending;
	atomic_t                             cdm_done;
	uint64_t                             last_cdm_done_req;
	struct completion                    config_done_complete;
	uint32_t                             hw_version;
	struct cam_cmd_buf_desc              reg_dump_buf_desc[
					CAM_REG_DUMP_MAX_BUF_ENTRIES];
	uint32_t                             num_reg_dump_buf;
	uint64_t                             applied_req_id;
	enum cam_ife_ctx_master_type         ctx_type;
	uint32_t                             ctx_config;
	struct timespec64                    ts;
	void                                *buf_done_controller;
	struct cam_ife_hw_mgr_ctx_scratch_buf_info scratch_buf_info;
	struct cam_ife_hw_mgr_ctx_flags      flags;
	struct cam_ife_hw_mgr_ctx_pf_info    pf_info;
	struct cam_ife_cdm_user_data         cdm_userdata;
	uint32_t                             bw_config_version;
	atomic_t                             recovery_id;
	uint32_t                             current_mup;
	uint32_t                             curr_num_exp;
	uint32_t                             try_recovery_cnt;
	uint64_t                             recovery_req_id;
	uint32_t                             drv_path_idle_en;
	uint32_t                             major_version;
	struct cam_isp_context_comp_record  *vfe_bus_comp_grp;
	struct cam_isp_context_comp_record  *sfe_bus_comp_grp;
	struct timespec64                    cdm_done_ts;
	bool                                 is_hw_ctx_acq;
	uint32_t                             acq_hw_ctxt_src_dst_map[CAM_ISP_MULTI_CTXT_MAX];
	atomic_t                             ctx_state;
	bool                                 is_offline;
	bool                                 waiting_start;
	uint32_t                             start_ctx_idx;
	bool                                 sfe_rd_only;
	uint32_t                             offline_clk;
	uint32_t                             offline_sfe_clk;
	struct cam_ife_virtual_rdi_mapping   mapping_table;
};

/**
 * struct cam_ife_offline_hw - Offline ife context allocation information
 *
 * @ctx_idx:                context index
 * @custom_enabled:         update the flag if context is connected to custom HW
 * @use_frame_header_ts     obtain qtimer ts using frame header
 * @acquired_hw_id:         Acquired hardware mask
 * @acquired_hw_path:       Acquired path mask for an input
 *                          if input splits into multiple paths,
 *                          its updated per hardware
 * valid_acquired_hw:       Valid num of acquired hardware
 * @ife_ctx:                HW context connected to that HW
 *
 */
struct cam_ife_offline_hw {
	uint32_t                        ctx_idx;
	bool                            custom_enabled;
	bool                            use_frame_header_ts;
	uint32_t                        acquired_hw_id[CAM_MAX_ACQ_RES];
	uint32_t                        acquired_hw_path[CAM_MAX_ACQ_RES][
						CAM_MAX_HW_SPLIT];
	uint32_t                        valid_acquired_hw;
	struct cam_ife_hw_concrete_ctx *ife_ctx;
};

/**
 * struct cam_ife_mgr_offline_in_queue - Offline IFE input request queue element
 *
 * @list:                   list private data;
 * @request_id:             request id
 * @ctx_idx:                context owning this request
 * @hw_id:                  ID of hardware core servicing this request
 * @prepare:                prepare requset data
 * @cfg:                    config request data
 * @ready:                  indicates if request is ready to be processed
 *
 */
struct cam_ife_mgr_offline_in_queue {
	struct list_head                  list;
	uint64_t                          request_id;
	uint32_t                          ctx_idx;
	uint32_t                          hw_id;
	struct cam_hw_prepare_update_args prepare;
	struct cam_hw_config_args         cfg;
	bool                              ready;
};

/**
 * struct cam_isp_bus_hw_caps - BUS capabilities
 *
 * @max_vfe_out_res_type  :  max ife out res type value from hw
 * @max_sfe_out_res_type  :  max sfe out res type value from hw
 * @num_ife_perf_counters :  max ife perf counters supported
 * @num_sfe_perf_counters :  max sfe perf counters supported
 * @support_consumed_addr :  indicate whether hw supports last consumed address
 */
struct cam_isp_ife_sfe_hw_caps {
	uint32_t     max_vfe_out_res_type;
	uint32_t     max_sfe_out_res_type;
	uint32_t     num_ife_perf_counters;
	uint32_t     num_sfe_perf_counters;
	bool         support_consumed_addr;
};

/*
 * struct cam_isp_sys_cache_info:
 *
 * @Brief:                   ISP Bus sys cache info. Placeholder for all cache ids and their
 *                           types
 *
 * @type:                    Cache type
 * @scid:                    Cache slice ID
 * @llcc_staling_support     to check llcc sys cache stalling mode supported or not
 */
struct cam_isp_sys_cache_info {
	enum cam_sys_cache_config_types type;
	int32_t                         scid;
	bool            llcc_staling_support;
};

/*
 * struct cam_isp_sfe_cache_info:
 *
 * @Brief:                   SFE cache info. Placeholder for:
 *                           1. Supported cache IDs which are populated during
 *                           probe based on large and small.
 *                           2. keeps track for the current cache id used for a
 *                           particular exposure type
 *                           3. keeps track of acvitated exposures.
 *                           Based on this data, we can toggle the SCIDs for a particular
 *                           hw whenever there is a hw halt. Also we don't change
 *                           the SCID in case of dynamic exposure switches.
 *
 * @supported_scid_idx:      Bit mask for IDs supported for each exposure type
 * @curr_idx:                Index of Cache ID in use for each exposure
 * @activated:               Maintains if the cache is activated for a particular exposure
 */
struct cam_isp_sfe_cache_info {
	int      supported_scid_idx;
	int      curr_idx[CAM_ISP_EXPOSURE_MAX];
	bool     activated[CAM_ISP_EXPOSURE_MAX];
};

/*
 * struct cam_isp_irq_inject_irq_desc: Structure to hold IRQ description
 *
 * @bitmask : Bitmask of the IRQ
 * @desc    : String to describe the IRQ bit
 */
struct cam_isp_irq_inject_irq_desc {
	uint32_t    bitmask;
	char       *desc;
};

/*
 * enum cam_isp_irq_inject_common_param_pos - Irq injection param
 *
 * HW_TYPE         : hw to inject IRQ
 * HW_IDX          : index of the selected hw
 * RES_ID          : register to set irq
 * IRQ_MASK        : IRQ to be triggered
 * INJECT_REQ      : req to trigger the IRQ
 * INJECT_PARAM_MAX: max allowed num of injected param
 */
enum cam_isp_irq_inject_common_param_pos {
	HW_TYPE,
	HW_IDX,
	REG_UNIT,
	IRQ_MASK,
	INJECT_REQ,
	INJECT_PARAM_MAX
};

/**
 * struct cam_ife_hw_mgr - IFE HW Manager
 *
 * @mgr_common:            common data for all HW managers
 * @csid_devices;          csid device instances array. This will be filled by
 *                         HW manager during the initialization.
 * @ife_devices:           IFE device instances array. This will be filled by
 *                         HW layer during initialization
 * @sfe_devices:           SFE device instance array
 * @ctx_mutex:             mutex for the hw context pool
 * @free_ctx_list:         free hw context list
 * @used_ctx_list:         used hw context list
 * @ctx_pool:              context storage
 * @virt_ctx_pool:         HW manager context storage
 * @csid_hw_caps           csid hw capability stored per core
 * @ife_dev_caps           ife device capability per core
 * @work q                 work queue for IFE hw manager
 * @debug_cfg              debug configuration
 * @ctx_lock               context lock
 * @hw_pid_support         hw pid support for this target
 * @csid_rup_en            Reg update at CSID side
 * @csid_global_reset_en   CSID global reset enable
 * @csid_camif_irq_support CSID camif IRQ support
 * @cam_ddr_drv_support    DDR DRV support
 * @cam_clk_drv_support    CLK DRV support
 * @isp_caps               Capability of underlying SFE/IFE HW
 * @path_port_map          Mapping of outport to IFE mux
 * @num_caches_found       Number of caches supported
 * @sys_cache_info         Sys cache info
 * @sfe_cache_info         SFE Cache Info
 * @isp_device_type:       If device supports single-context(ife) or multi-
 *                         context(mc_tfe)
 * @irq_inject_param       Param for isp irq injection
 * @num_acquired_offline_ctx:  number of acquired offline contexts
 * @acquired_hw_pool:      offline contexts acquire data
 * @input_queue:           input request queue for offline processing
 * @in_proc_queue:         currently processed requests queuse
 * @starting_offline_cnt:  number of offline HWs that are currently starting
 * @offline_clk:           offline ife clock
 * @offline_sfe_clk:       offline sfe clock
 * @max_clk_threshold:     Peak sfe clock
 * @nom_clk_threshold:     nominal sfe clock
 * @min_clk_threshold:     Min sfe clock
 * @offline_reconfig:      offline ISP need to reconfigure or not
 */
struct cam_ife_hw_mgr {
	struct cam_isp_hw_mgr          mgr_common;
	struct cam_hw_intf            *csid_devices[CAM_IFE_CSID_HW_NUM_MAX];
	struct cam_isp_hw_intf_data   *ife_devices[CAM_IFE_HW_NUM_MAX];
	struct cam_isp_hw_intf_data   *sfe_devices[CAM_SFE_HW_NUM_MAX];
	struct cam_soc_reg_map        *cdm_reg_map[CAM_IFE_HW_NUM_MAX];

	struct mutex                   ctx_mutex;
	atomic_t                       active_ctx_cnt;
	struct list_head               free_ctx_list;
	struct list_head               used_ctx_list;
	struct cam_ife_hw_concrete_ctx ctx_pool[CAM_IFE_CTX_MAX];
	struct cam_ife_hw_mgr_ctx      virt_ctx_pool[CAM_IFE_CTX_MAX];
	struct cam_ife_csid_hw_caps      csid_hw_caps[
						CAM_IFE_CSID_HW_NUM_MAX];
	struct cam_vfe_hw_get_hw_cap     ife_dev_caps[CAM_IFE_HW_NUM_MAX];
	struct cam_req_mgr_core_workq   *workq;
	struct cam_ife_hw_mgr_debug      debug_cfg;
	spinlock_t                       ctx_lock;
	bool                             hw_pid_support;
	bool                             csid_rup_en;
	bool                             csid_global_reset_en;
	bool                             csid_camif_irq_support;
	bool                             cam_ddr_drv_support;
	bool                             cam_clk_drv_support;
	struct cam_isp_ife_sfe_hw_caps   isp_caps;
	struct cam_isp_hw_path_port_map  path_port_map;
	uint32_t                         num_caches_found;
	struct cam_isp_sys_cache_info    sys_cache_info[CAM_LLCC_MAX];
	struct cam_isp_sfe_cache_info    sfe_cache_info[CAM_SFE_HW_NUM_MAX];
	uint32_t                         isp_device_type;
	struct cam_isp_irq_inject_param  irq_inject_param[MAX_INJECT_SET];
	atomic_t                         num_acquired_offline_ctx;
	struct cam_ife_offline_hw        acquired_hw_pool[CAM_IFE_CTX_MAX];
	struct cam_ife_mgr_offline_in_queue   input_queue;
	struct cam_ife_mgr_offline_in_queue   in_proc_queue;
	uint32_t                         starting_offline_cnt;
	uint32_t                         max_ife_lite_out_res;
	uint32_t                         offline_clk;
	uint32_t                         offline_sfe_clk;
	uint32_t                         max_clk_threshold;
	uint32_t                         nom_clk_threshold;
	uint32_t                         min_clk_threshold;
	uint32_t                         bytes_per_clk;
	bool                             offline_reconfig;
};

/**
 * struct cam_ife_hw_mgr_sensor_stream_config  -  camera sensor stream configurations
 *
 * @priv                        : Context data
 * @sensor_id                   : camera sensor unique index
 * @contextId                   : sensor context id to which this vc/dt belongs to
 * @color_filter_arrangement    : indicates YUV CHROMA Downscale conversion enabled
 * @num_valid_vc_dt_pxl         : valid vc and dt for pxl path
 * @num_valid_vc_dt_rdi         : valid vc and dt in array for rdi path
 * @pxl_vc                      : input virtual channel number for pxl path
 * @pxl_dt                      : input data type number for pxl path
 * @ppp_vc                      : input virtual channel number for ppp path
 * @ppp_dt                      : input data type number for ppp path
 * @lcr_vc                      : input virtual channel number for lcr path
 * @lcr_dt                      : input data type number for lcr path
 * @rdi_vc                      : input virtual channel number for rdi path
 * @rdi_dt                      : input data type number for rdi path
 * @error_threshold             : Error Threshold
 * @sync_id                     : if sensors are in sync then it indicates which
 *                                all sensors are in sync, sharing same sync id.
 *                                syncid = -1 indicates sensor is not in sync mode
 * @frame_freeze_count          : if calculated CRC value is same for consecutive
 *                                frames then it is frame freeze.
 *                                frame freeze count indicates tolerable rate for
 *                                consecutive frame freezes
 * @decode_format               : input data format
 * @rdi_vc_dt_updated           : Indicates count of rdi vc-dt associated to any hw res
 * @yuv_rdi_vc_dt_updated       : Indicates vc dt is updated for rdi path with yuv conversion
 * @pxl_vc_dt_updated           : Indicates if pxl vc-dt is associated to any hw res
 * @lcr_vc_dt_updated           : Indicates if lcr vc-dt associated to any hw res
 * @ppp_vc_dt_updated           : Indicates if ppp vc-dt is associated to any hw res
 * @acquired                    : indicates whether acquire is done for this sensor id
 * @is_streamon                 : indicates whether streamon is done for this sensor id
 */
struct cam_ife_hw_mgr_sensor_stream_config {
	void                                      *priv;
	uint32_t                                   sensor_id;
	uint32_t                                   context_id;
	uint32_t                                   color_filter_arrangement;
	uint32_t                                   num_valid_vc_dt_pxl;
	uint32_t                                   num_valid_vc_dt_rdi;
	uint32_t                                   num_valid_vc_dt_ppp;
	uint32_t                                   num_valid_vc_dt_lcr;
	uint32_t                                   pxl_vc;
	uint32_t                                   pxl_dt;
	uint32_t                                   ppp_vc;
	uint32_t                                   ppp_dt;
	uint32_t                                   lcr_vc;
	uint32_t                                   lcr_dt;
	uint32_t                                   rdi_vc[CAM_ISP_VC_DT_CFG];
	uint32_t                                   rdi_dt[CAM_ISP_VC_DT_CFG];
	uint32_t                                   error_threshold;
	uint32_t                                   sync_id;
	uint32_t                                   frame_freeze_count;
	uint32_t                                   decode_format;
	uint32_t                                   rdi_vc_dt_updated;
	bool                                       yuv_rdi_vc_dt_updated;
	bool                                       pxl_vc_dt_updated;
	bool                                       lcr_vc_dt_updated;
	bool                                       ppp_vc_dt_updated;
	bool                                       acquired;
	bool                                       is_streamon;
};

/**
 * struct cam_ife_hw_mgr_stream_grp_config  -  camera sensor stream group configurations
 *
 * @res_type                      : input resource type
 * @lane_type                     : lane type: c-phy or d-phy.
 * @lane_num                      : active lane number
 * @lane_cfg                      : lane configurations: 4 bits per lane
 * @feature_mask                  : feature flag
 * @acquire_cnt                   : count of number of acquire calls
 * @stream_cfg_cnt                : number of sensor configurations for pxl and rdi paths
 * @rdi_stream_cfg_cnt            : number of sensor configurations for only rdi path
 * @rdi_yuv_conversion_stream_cnt : Indicates how many yuv sensors need yuv 420 conversions
 * @hw_ctx_cnt                    : count of number of hw ctx
 * @stream_on_cnt                 : count of number of streamon calls for this ife device
 * @enable_error_recovery         : indicates error recovery is enabled/disabled
 * @recovery_threshold            : indicates recovery threshold
 * @res_ife_csid_list             : CSID resource list
 * @res_ife_src_list              : IFE input resource list
 * @res_list_ife_out              : IFE output resources array
 * @lock                          : mutex lock
 * @free_res_list                 : Free resources list for the branch node
 * @acquired_hw_idx               : Index of acquired HW
 * @res_pool                      : memory storage for the free resource list
 * @mapping_table                 : mapping table between UMd and KMD RDI resources
 * @stream_cfg                    : stream config data
 * @recovery_in_progress          : Indicates if ife is process of frame drop recovery
 */
struct cam_ife_hw_mgr_stream_grp_config {
	uint32_t                                      res_type;
	uint32_t                                      lane_type;
	uint32_t                                      lane_num;
	uint32_t                                      lane_cfg;
	uint32_t                                      feature_mask;
	uint32_t                                      acquire_cnt;
	uint32_t                                      stream_cfg_cnt;
	uint32_t                                      rdi_stream_cfg_cnt;
	uint32_t                                      rdi_yuv_conversion_stream_cnt;
	uint32_t                                      hw_ctx_cnt;
	bool                                          enable_error_recovery;
	uint32_t                                      recovery_threshold;
	int32_t                                       stream_on_cnt;
	struct list_head                              res_ife_csid_list;
	struct list_head                              res_ife_src_list;
	struct cam_isp_hw_mgr_res                    *res_list_ife_out;
	struct mutex                                  lock;
	struct list_head                              free_res_list;
	uint32_t                                      acquired_hw_idx;
	struct cam_isp_hw_mgr_res                     res_pool[CAM_IFE_HW_STREAM_GRP_RES_POOL_MAX];
	struct cam_ife_hw_mgr_sensor_stream_config    stream_cfg[CAM_ISP_STREAM_CFG_MAX];
	bool                                          recovery_in_progress;
};

/**
 * struct cam_ife_hw_mgr_sensor_grp_cfg  -  sensor group configurations
 *
 * @num_grp_cfg                 : count of total active group configs
 * @grp_cfg                     : stream group data
 */
struct cam_ife_hw_mgr_sensor_grp_cfg {
	uint32_t                                  num_grp_cfg;
	struct cam_ife_hw_mgr_stream_grp_config  *grp_cfg[CAM_ISP_STREAM_GROUP_CFG_MAX];
};

/**
 * struct cam_ife_hw_event_recovery_data - Payload for the recovery procedure
 *
 * @error_type:               Error type that causes the recovery
 * @affected_core:            Array of the hardware cores that are affected
 * @affected_ctx:             Array of the hardware contexts that are affected
 * @no_of_context:            Actual number of the affected context
 * @id:                       Unique ID associated with this recovery data (per HW context)
 *
 */
struct cam_ife_hw_event_recovery_data {
	uint32_t                        error_type;
	uint32_t                        affected_core[CAM_ISP_HW_NUM_MAX];
	struct cam_ife_hw_concrete_ctx *affected_ctx[CAM_IFE_CTX_MAX];
	uint32_t                        no_of_context;
	uint32_t                        id[CAM_IFE_CTX_MAX];
};

/**
 * struct cam_ife_hw_mini_dump_ctx - Mini dump data
 *
 * @base:                   device base index array contain the all IFE HW
 * @pf_info:                Page Fault Info
 * @csid_md:                CSID mini dump data
 * @vfe_md:                 VFE mini dump data
 * @sfe_md:                 SFE mini dump data
 * @flags:                  Flags pertainting to this ctx
 * @ctx_priv:               Array of the hardware contexts that are affected
 * @last_cdm_done_req:      Last cdm done request
 * @applied_req_id:         Last request id to be applied
 * @cdm_handle:             cdm hw acquire handle
 * @ctx_index:              acquired context id.
 * @left_hw_idx:            hw index for master core [left]
 * @right_hw_idx:           hw index for slave core [right]
 * @num_base:               number of valid base data in the base array
 * @cdm_id:                 cdm id of the acquired cdm
 * @ctx_type:               Type of IFE ctx [CUSTOM/SFE etc.]
 * @overflow_pending:       flat to specify the overflow is pending for the
 * @cdm_done:               flag to indicate cdm has finished writing shadow
 *                          registers
 */
struct cam_ife_hw_mini_dump_ctx {
	struct cam_isp_ctx_base_info          base[CAM_IFE_HW_NUM_MAX +
				         	CAM_SFE_HW_NUM_MAX];
	struct cam_ife_hw_mgr_ctx_pf_info     pf_info;
	void                                 *csid_md[CAM_IFE_HW_NUM_MAX];
	void                                 *vfe_md[CAM_IFE_HW_NUM_MAX];
	void                                 *sfe_md[CAM_SFE_HW_NUM_MAX];
	struct cam_ife_hw_mgr_ctx_flags       flags;
	void                                 *ctx_priv;
	uint64_t                              last_cdm_done_req;
	uint64_t                              applied_req_id;
	uint32_t                              cdm_handle;
	uint8_t                               ctx_index;
	uint8_t                               left_hw_idx;
	uint8_t                               right_hw_idx;
	uint8_t                               num_base;
	enum cam_cdm_id                       cdm_id;
	enum cam_ife_ctx_master_type          ctx_type;
	bool                                  overflow_pending;
	bool                                  cdm_done;
};

/**
 * struct cam_ife_hw_mini_dump_data - Mini dump data
 *
 * @num_ctx:                  Number of context dumped
 * @ctx:                      Array of context
 *
 */
struct cam_ife_hw_mini_dump_data {
	uint32_t                            num_ctx;
	struct cam_ife_hw_mini_dump_ctx    *ctx[CAM_IFE_CTX_MAX];
};

/**
 * cam_ife_hw_mgr_init()
 *
 * @brief:              Initialize the IFE hardware manger. This is the
 *                      etnry functinon for the IFE HW manager.
 *
 * @hw_mgr_intf:        IFE hardware manager object returned
 * @iommu_hdl:          Iommu handle to be returned
 * @isp_device_type:    If device supports single-context(ife) or multi-
 *                      context(mc_tfe)
 *
 */
int cam_ife_hw_mgr_init(struct cam_hw_mgr_intf *hw_mgr_intf, int *iommu_hdl,
	uint32_t isp_device_type);
void cam_ife_hw_mgr_deinit(void);
int cam_ife_mgr_config_hw(void *hw_mgr_priv, void *config_hw_args);
int cam_ife_mgr_prepare_hw_update(void *hw_mgr_priv, void *prepare_hw_update_args);
int cam_ife_mgr_update_offline_ife_out(struct cam_ife_hw_mgr_ctx *ife_ctx);
int cam_convert_hw_idx_to_ife_hw_num(int hw_idx);
int cam_ife_hw_mgr_get_res(struct list_head *src_list, struct cam_isp_hw_mgr_res **res);
int cam_ife_hw_mgr_event_handler(void *priv, uint32_t evt_id, void *evt_info);
int cam_ife_hw_mgr_put_res(struct list_head *src_list, struct cam_isp_hw_mgr_res  **res);
int cam_ife_hw_mgr_is_rdi_res(uint32_t res_id);
bool cam_isp_is_ctx_primary_rdi(struct cam_ife_hw_mgr_ctx  *ctx);
bool cam_ife_hw_mgr_is_ife_out_port(uint32_t res_id);
bool cam_ife_hw_mgr_check_path_port_compat(uint32_t in_type, uint32_t out_type);
int cam_convert_rdi_out_res_id_to_src(int res_id);
enum cam_ife_pix_path_res_id cam_ife_hw_mgr_get_ife_csid_rdi_res_type(uint32_t out_port_type);
int cam_ife_hw_mgr_acquire_res_ife_csid_pxl(struct cam_ife_hw_mgr_ctx *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port, bool is_ipp, bool crop_enable,
	int index);
int cam_ife_hw_mgr_acquire_res_ife_csid_rdi(struct cam_ife_hw_mgr_ctx  *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port,
	uint32_t *acquired_rdi_res, int index, bool per_port_acquire);
int cam_ife_hw_mgr_acquire_res_ife_src(struct cam_ife_hw_mgr_ctx *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port, bool acquire_lcr, bool acquire_ppp,
	bool is_rdi_res, uint32_t *acquired_hw_id, uint32_t *acquired_hw_path,
	uint32_t res_path_id, int index);
int cam_ife_hw_mgr_acquire_res_ife_out(struct cam_ife_hw_mgr_ctx *ife_ctx,
	struct cam_isp_in_port_generic_info *in_port, int index);
int cam_ife_mgr_csid_stop_hw(struct cam_ife_hw_mgr_ctx *ctx, struct list_head  *stop_list,
		uint32_t  base_idx, uint32_t stop_cmd);
int cam_ife_mgr_finish_clk_bw_update(struct cam_ife_hw_mgr_ctx *ctx,
	uint64_t request_id, bool skip_clk_data_rst);
void cam_ife_hw_mgr_stop_hw_res(struct cam_isp_hw_mgr_res *isp_hw_res);
int cam_ife_hw_mgr_start_hw_res(struct cam_isp_hw_mgr_res *isp_hw_res,
	struct cam_ife_hw_mgr_ctx *ctx);
int cam_ife_hw_mgr_free_hw_res(struct cam_isp_hw_mgr_res *isp_hw_res, bool del_list);
#endif /* _CAM_IFE_HW_MGR_H_ */
