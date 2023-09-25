/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_HFI_H_
#define _IRIS_HFI_H_

#include "iris_instance.h"
#include "iris_core.h"

int iris_hfi_core_init(struct iris_core *core);
int iris_hfi_core_deinit(struct iris_core *core);
int iris_hfi_session_open(struct iris_inst *inst);
int iris_hfi_session_close(struct iris_inst *inst);
int iris_hfi_set_property(struct iris_inst *inst,
			  u32 packet_type, u32 flag, u32 plane, u32 payload_type,
			  void *payload, u32 payload_size);

irqreturn_t iris_hfi_isr(int irq, void *data);
irqreturn_t iris_hfi_isr_handler(int irq, void *data);
int iris_hfi_queue_buffer(struct iris_inst *inst,
			  struct iris_buffer *buffer);
int iris_hfi_release_buffer(struct iris_inst *inst,
			    struct iris_buffer *buffer);

#endif
