// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_hfi_packet.h"
#include "hfi_defines.h"

static int hfi_create_header(u8 *packet, u32 packet_size, u32 session_id,
			     u32 header_id)
{
	struct hfi_header *hdr = (struct hfi_header *)packet;

	if (!packet || packet_size < sizeof(*hdr))
		return -EINVAL;

	memset(hdr, 0, sizeof(*hdr));

	hdr->size = sizeof(*hdr);
	hdr->session_id = session_id;
	hdr->header_id = header_id;
	hdr->num_packets = 0;

	return 0;
}

static int hfi_create_packet(u8 *packet, u32 packet_size, u32 pkt_type,
			     u32 pkt_flags, u32 payload_type, u32 port,
			     u32 packet_id, void *payload, u32 payload_size)
{
	struct hfi_header *hdr;
	struct hfi_packet *pkt;
	u32 pkt_size;

	if (!packet)
		return -EINVAL;

	hdr = (struct hfi_header *)packet;
	if (hdr->size < sizeof(*hdr))
		return -EINVAL;

	pkt = (struct hfi_packet *)(packet + hdr->size);
	pkt_size = sizeof(*pkt) + payload_size;
	if (packet_size < hdr->size  + pkt_size)
		return -EINVAL;

	memset(pkt, 0, pkt_size);
	pkt->size = pkt_size;
	pkt->type = pkt_type;
	pkt->flags = pkt_flags;
	pkt->payload_info = payload_type;
	pkt->port = port;
	pkt->packet_id = packet_id;
	if (payload_size)
		memcpy((u8 *)pkt + sizeof(*pkt),
		       payload, payload_size);

	hdr->num_packets++;
	hdr->size += pkt->size;

	return 0;
}

int hfi_packet_sys_init(struct iris_core *core,
			u8 *pkt, u32 pkt_size)
{
	u32 payload = 0;
	int ret;

	ret = hfi_create_header(pkt, pkt_size,
				0,
				core->header_id++);
	if (ret)
		goto error;

	payload = HFI_VIDEO_ARCH_LX;
	core->sys_init_id = core->packet_id++;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_CMD_INIT,
				(HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				HFI_HOST_FLAGS_INTR_REQUIRED |
				HFI_HOST_FLAGS_NON_DISCARDABLE),
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->sys_init_id,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->max_channels;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_MAX_CHANNELS,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->mal_length;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_MAL_LENGTH,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->highest_bank_bit;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_HBB,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->bank_swzl_level;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_BANK_SWZL_LEVEL1,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->bank_swz2_level;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_BANK_SWZL_LEVEL2,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->bank_swz3_level;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_BANK_SWZL_LEVEL3,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	payload = core->platform_data->ubwc_config->bank_spreading;
	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_UBWC_BANK_SPREADING,
				HFI_HOST_FLAGS_NONE,
				HFI_PAYLOAD_U32,
				HFI_PORT_NONE,
				core->packet_id++,
				&payload,
				sizeof(u32));
	if (ret)
		goto error;

	return ret;

error:
	dev_err(core->dev, "%s: create sys init packet failed\n", __func__);

	return ret;
}

int hfi_packet_image_version(struct iris_core *core,
			     u8 *pkt, u32 pkt_size)
{
	int ret;

	ret = hfi_create_header(pkt, pkt_size,
				0,
				core->header_id++);
	if (ret)
		goto error;

	ret = hfi_create_packet(pkt, pkt_size,
				HFI_PROP_IMAGE_VERSION,
				(HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				HFI_HOST_FLAGS_INTR_REQUIRED |
				HFI_HOST_FLAGS_GET_PROPERTY),
				HFI_PAYLOAD_NONE,
				HFI_PORT_NONE,
				core->packet_id++,
				NULL, 0);
	if (ret)
		goto error;

	return ret;

error:
	dev_err(core->dev, "%s: create image version packet failed\n", __func__);

	return ret;
}

int hfi_packet_session_command(struct iris_inst *inst, u32 pkt_type,
			       u32 flags, u32 port, u32 session_id,
			       u32 payload_type, void *payload,
			       u32 payload_size)
{
	struct iris_core *core;
	int ret;

	core = inst->core;

	ret = hfi_create_header(inst->packet, inst->packet_size,
				session_id, core->header_id++);
	if (ret)
		return ret;

	ret = hfi_create_packet(inst->packet,
				inst->packet_size,
				pkt_type,
				flags,
				payload_type,
				port,
				core->packet_id++,
				payload,
				payload_size);

	return ret;
}
