// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hfi_defines.h"
#include "iris_hfi_packet.h"
#include "iris_hfi_queue.h"
#include "iris_hfi_response.h"

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

static int validate_packet(u8 *response_pkt, u8 *core_resp_pkt,
			   u32 core_resp_pkt_size)
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

static int validate_hdr_packet(struct iris_core *core,
			       struct hfi_header *hdr)
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

static int handle_system_error(struct iris_core *core,
			       struct hfi_packet *pkt)
{
	print_sfr_message(core);

	iris_core_deinit(core);

	return 0;
}

static int handle_response(struct iris_core *core, void *response)
{
	struct hfi_header *hdr;
	int ret;

	hdr = (struct hfi_header *)response;
	ret = validate_hdr_packet(core, hdr);
	if (ret)
		return handle_system_error(core, NULL);

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
