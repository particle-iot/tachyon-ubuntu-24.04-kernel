// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_helpers.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_queue.h"
#include "iris_hfi_response.h"
#include "iris_vdec.h"

struct iris_core_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_core *core, struct hfi_packet *pkt);
};

struct iris_inst_hfi_range {
	u32 begin;
	u32 end;
	int (*handle)(struct iris_inst *inst, struct hfi_packet *pkt);
};

struct iris_hfi_packet_handle {
	enum hfi_buffer_type type;
	int (*handle)(struct iris_inst *inst, struct hfi_packet *pkt);
};

static void print_sfr_message(struct iris_core *core)
{
	struct sfr_buffer *vsfr = NULL;
	u32 vsfr_size = 0;
	void *p = NULL;

	vsfr = (struct sfr_buffer *)core->sfr.kernel_vaddr;
	if (vsfr) {
		if (vsfr->bufsize != core->sfr.size)
			return;
		vsfr_size = vsfr->bufsize - sizeof(u32);
		p = memchr(vsfr->rg_data, '\0', vsfr_size);
	/* SFR isn't guaranteed to be NULL terminated */
		if (!p)
			vsfr->rg_data[vsfr_size - 1] = '\0';
	}
}

static int validate_packet(u8 *response_pkt, u8 *core_resp_pkt, u32 core_resp_pkt_size)
{
	u32 response_pkt_size = 0;
	u8 *response_limit;

	if (!response_pkt || !core_resp_pkt || !core_resp_pkt_size)
		return -EINVAL;

	response_limit = core_resp_pkt + core_resp_pkt_size;

	if (response_pkt < core_resp_pkt || response_pkt > response_limit)
		return -EINVAL;

	response_pkt_size = *(u32 *)response_pkt;
	if (!response_pkt_size)
		return -EINVAL;

	if (response_pkt_size < sizeof(struct hfi_packet))
		return -EINVAL;

	if (response_pkt + response_pkt_size > response_limit)
		return -EINVAL;

	return 0;
}

static int validate_hdr_packet(struct iris_core *core, struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	int i, ret = 0;
	u8 *pkt;

	if (hdr->size < sizeof(*hdr) + sizeof(*packet))
		return -EINVAL;

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));

	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet *)pkt;
		ret = validate_packet(pkt, core->response_packet, core->packet_size);
		if (ret)
			return ret;

		pkt += packet->size;
	}

	return ret;
}

static int handle_session_error(struct iris_inst *inst,
				struct hfi_packet *pkt)
{
	struct iris_core *core;
	char *error;

	core = inst->core;

	switch (pkt->type) {
	case HFI_ERROR_MAX_SESSIONS:
		error = "exceeded max sessions";
		break;
	case HFI_ERROR_UNKNOWN_SESSION:
		error = "unknown session id";
		break;
	case HFI_ERROR_INVALID_STATE:
		error = "invalid operation for current state";
		break;
	case HFI_ERROR_INSUFFICIENT_RESOURCES:
		error = "insufficient resources";
		break;
	case HFI_ERROR_BUFFER_NOT_SET:
		error = "internal buffers not set";
		break;
	case HFI_ERROR_FATAL:
		error = "fatal error";
		break;
	default:
		error = "unknown";
		break;
	}

	dev_err(core->dev, "session error received %#x: %s\n",
		pkt->type, error);
	iris_inst_change_state(inst, IRIS_INST_ERROR);

	return 0;
}

static int handle_system_error(struct iris_core *core,
			       struct hfi_packet *pkt)
{
	print_sfr_message(core);

	iris_core_deinit(core);

	return 0;
}

static int handle_system_init(struct iris_core *core,
			      struct hfi_packet *pkt)
{
	if (!(pkt->flags & HFI_FW_FLAGS_SUCCESS))
		return 0;

	mutex_lock(&core->lock);
	if (pkt->packet_id != core->sys_init_id)
		goto unlock;

	iris_change_core_state(core, IRIS_CORE_INIT);

unlock:
	mutex_unlock(&core->lock);

	return 0;
}

static int handle_session_close(struct iris_inst *inst,
				struct hfi_packet *pkt)
{
	signal_session_msg_receipt(inst, SIGNAL_CMD_CLOSE);

	return 0;
}

static int handle_src_change(struct iris_inst *inst,
			     struct hfi_packet *pkt)
{
	int ret = 0;

	if (pkt->port == HFI_PORT_BITSTREAM)
		ret = vdec_src_change(inst);
	else if (pkt->port == HFI_PORT_RAW)
		ret = 0;

	if (ret)
		iris_inst_change_state(inst, IRIS_INST_ERROR);

	return ret;
}

static int handle_session_command(struct iris_inst *inst,
				  struct hfi_packet *pkt)
{
	int i, ret = 0;
	static const struct iris_hfi_packet_handle hfi_pkt_handle[] = {
		{HFI_CMD_OPEN,              NULL                    },
		{HFI_CMD_CLOSE,             handle_session_close    },
		{HFI_CMD_SETTINGS_CHANGE,   handle_src_change       },
		{HFI_CMD_SUBSCRIBE_MODE,    NULL                    },
	};

	for (i = 0; i < ARRAY_SIZE(hfi_pkt_handle); i++) {
		if (hfi_pkt_handle[i].type == pkt->type) {
			if (hfi_pkt_handle[i].handle) {
				ret = hfi_pkt_handle[i].handle(inst, pkt);
				if (ret)
					return ret;
			}
			break;
		}
	}

	if (i == ARRAY_SIZE(hfi_pkt_handle))
		return -EINVAL;

	return ret;
}

static int handle_dpb_list_property(struct iris_inst *inst,
				    struct hfi_packet *pkt)
{
	u8 *payload_start;
	u32 payload_size;

	payload_size = pkt->size - sizeof(*pkt);
	payload_start = (u8 *)((u8 *)pkt + sizeof(*pkt));
	memset(inst->dpb_list_payload, 0, MAX_DPB_LIST_ARRAY_SIZE);

	if (payload_size > MAX_DPB_LIST_PAYLOAD_SIZE) {
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -EINVAL;
	}

	memcpy(inst->dpb_list_payload, payload_start, payload_size);

	return 0;
}

static int handle_session_property(struct iris_inst *inst,
				   struct hfi_packet *pkt)
{
	u32 *payload_ptr = NULL;
	int ret = 0;

	if (pkt->port != HFI_PORT_BITSTREAM)
		return 0;

	if (pkt->flags & HFI_FW_FLAGS_INFORMATION)
		return 0;

	payload_ptr = (u32 *)((u8 *)pkt + sizeof(*pkt));
	if (!payload_ptr)
		return -EINVAL;

	switch (pkt->type) {
	case HFI_PROP_BITSTREAM_RESOLUTION:
		inst->src_subcr_params.bitstream_resolution = payload_ptr[0];
		break;
	case HFI_PROP_CROP_OFFSETS:
		inst->src_subcr_params.crop_offsets[0] = payload_ptr[0];
		inst->src_subcr_params.crop_offsets[1] = payload_ptr[1];
		break;
	case HFI_PROP_LUMA_CHROMA_BIT_DEPTH:
		inst->src_subcr_params.bit_depth = payload_ptr[0];
		break;
	case HFI_PROP_CODED_FRAMES:
		inst->src_subcr_params.coded_frames = payload_ptr[0];
		break;
	case HFI_PROP_BUFFER_FW_MIN_OUTPUT_COUNT:
		inst->src_subcr_params.fw_min_count = payload_ptr[0];
		break;
	case HFI_PROP_PIC_ORDER_CNT_TYPE:
		inst->src_subcr_params.pic_order_cnt = payload_ptr[0];
		break;
	case HFI_PROP_SIGNAL_COLOR_INFO:
		inst->src_subcr_params.color_info = payload_ptr[0];
		break;
	case HFI_PROP_PROFILE:
		inst->src_subcr_params.profile = payload_ptr[0];
		break;
	case HFI_PROP_LEVEL:
		inst->src_subcr_params.level = payload_ptr[0];
		break;
	case HFI_PROP_TIER:
		inst->src_subcr_params.tier = payload_ptr[0];
		break;
	case HFI_PROP_PICTURE_TYPE:
		inst->hfi_frame_info.picture_type = payload_ptr[0];
		break;
	case HFI_PROP_CABAC_SESSION:
		if (payload_ptr[0] == 1)
			inst->cap[ENTROPY_MODE].value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
		else
			inst->cap[ENTROPY_MODE].value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;
		break;
	case HFI_PROP_DPB_LIST:
		ret = handle_dpb_list_property(inst, pkt);
		if (ret)
			break;
		break;
	case HFI_PROP_NO_OUTPUT:
		inst->hfi_frame_info.no_output = 1;
		break;
	case HFI_PROP_QUALITY_MODE:
	case HFI_PROP_STAGE:
	case HFI_PROP_PIPE:
		break;
	default:
		break;
	}

	return ret;
}

static int handle_image_version_property(struct iris_core *core,
					 struct hfi_packet *pkt)
{
	u8 *str_image_version;
	u32 req_bytes;
	u32 i = 0;

	req_bytes = pkt->size - sizeof(*pkt);
	if (req_bytes < IRIS_VERSION_LENGTH - 1)
		return -EINVAL;

	str_image_version = (u8 *)pkt + sizeof(*pkt);

	for (i = 0; i < IRIS_VERSION_LENGTH - 1; i++) {
		if (str_image_version[i] != '\0')
			core->fw_version[i] = str_image_version[i];
		else
			core->fw_version[i] = ' ';
	}
	core->fw_version[i] = '\0';

	return 0;
}

static int handle_system_property(struct iris_core *core,
				  struct hfi_packet *pkt)
{
	int ret = 0;

	switch (pkt->type) {
	case HFI_PROP_IMAGE_VERSION:
		ret = handle_image_version_property(core, pkt);
		break;
	default:
		break;
	}

	return ret;
}

static int handle_system_response(struct iris_core *core,
				  struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	u8 *pkt, *start_pkt;
	int ret = 0;
	int i, j;
	static const struct iris_core_hfi_range be[] = {
		{HFI_SYSTEM_ERROR_BEGIN,   HFI_SYSTEM_ERROR_END,   handle_system_error     },
		{HFI_PROP_BEGIN,           HFI_PROP_END,           handle_system_property  },
		{HFI_CMD_BEGIN,            HFI_CMD_END,            handle_system_init      },
	};

	start_pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SYSTEM_ERROR) {
				ret = handle_system_error(core, packet);
				return ret;
			}

			if (packet->type > be[i].begin && packet->type < be[i].end) {
				ret = be[i].handle(core, packet);
				if (ret)
					return ret;

				if (packet->type >  HFI_SYSTEM_ERROR_BEGIN &&
				    packet->type < HFI_SYSTEM_ERROR_END)
					return 0;
			}
			pkt += packet->size;
		}
	}

	return ret;
}

static int handle_session_response(struct iris_core *core,
				   struct hfi_header *hdr)
{
	struct hfi_packet *packet;
	struct iris_inst *inst;
	u8 *pkt, *start_pkt;
	int ret = 0;
	int i, j;
	static const struct iris_inst_hfi_range be[] = {
		{HFI_SESSION_ERROR_BEGIN,  HFI_SESSION_ERROR_END,  handle_session_error    },
		{HFI_PROP_BEGIN,           HFI_PROP_END,           handle_session_property },
		{HFI_CMD_BEGIN,            HFI_CMD_END,            handle_session_command  },
	};

	inst = to_instance(core, hdr->session_id);
	if (!inst)
		return -EINVAL;

	mutex_lock(&inst->lock);
	memset(&inst->hfi_frame_info, 0, sizeof(struct iris_hfi_frame_info));

	pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < hdr->num_packets; i++) {
		packet = (struct hfi_packet *)pkt;
		if (packet->type == HFI_CMD_SETTINGS_CHANGE) {
			if (packet->port == HFI_PORT_BITSTREAM) {
				vdec_init_src_change_param(inst);
				break;
			}
		}
		pkt += packet->size;
	}

	start_pkt = (u8 *)((u8 *)hdr + sizeof(*hdr));
	for (i = 0; i < ARRAY_SIZE(be); i++) {
		pkt = start_pkt;
		for (j = 0; j < hdr->num_packets; j++) {
			packet = (struct hfi_packet *)pkt;
			if (packet->flags & HFI_FW_FLAGS_SESSION_ERROR)
				handle_session_error(inst, packet);

			if (packet->type > be[i].begin && packet->type < be[i].end) {
				ret = be[i].handle(inst, packet);
				if (ret)
					iris_inst_change_state(inst, IRIS_INST_ERROR);
			}
			pkt += packet->size;
		}
	}

	memset(&inst->hfi_frame_info, 0, sizeof(struct iris_hfi_frame_info));
	mutex_unlock(&inst->lock);

	return ret;
}

static int handle_response(struct iris_core *core, void *response)
{
	struct hfi_header *hdr;
	int ret;

	hdr = (struct hfi_header *)response;
	ret = validate_hdr_packet(core, hdr);
	if (ret)
		return handle_system_error(core, NULL);

	if (!hdr->session_id)
		return handle_system_response(core, hdr);
	else
		return handle_session_response(core, hdr);

	return ret;
}

int __response_handler(struct iris_core *core)
{
	int ret = 0;

	if (call_vpu_op(core, watchdog, core, core->intr_status)) {
		struct hfi_packet pkt = {.type = HFI_SYS_ERROR_WD_TIMEOUT};

		mutex_lock(&core->lock);
		iris_change_core_state(core, IRIS_CORE_ERROR);
		dev_err(core->dev, "%s: CPU WD error received\n", __func__);
		mutex_unlock(&core->lock);

		return handle_system_error(core, &pkt);
	}

	memset(core->response_packet, 0, core->packet_size);
	while (!iris_hfi_queue_msg_read(core, core->response_packet)) {
		ret = handle_response(core, core->response_packet);
		if (ret)
			continue;
		if (core->state != IRIS_CORE_INIT)
			break;
		memset(core->response_packet, 0, core->packet_size);
	}

	iris_flush_debug_queue(core, core->response_packet, core->packet_size);

	return ret;
}
