// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/qcom-dma-mapping.h>
#include <linux/mem-buf.h>
#include <soc/qcom/secure_buffer.h>

#include "msm_vidc_core.h"
#include "msm_vidc_driver.h"
#include "msm_vidc_debug.h"
#include "msm_vidc_events.h"
#include "msm_vidc_platform.h"
#include "msm_vidc_memory.h"

static u32 msm_vidc_buffer_region_ext(struct msm_vidc_inst *inst,
	enum msm_vidc_buffer_type buffer_type)
{
	u32 region = MSM_VIDC_NON_SECURE;

    switch (buffer_type) {
    case MSM_VIDC_BUF_ARP:
        region = MSM_VIDC_NON_SECURE;
        break;
    case MSM_VIDC_BUF_INPUT:
        if (is_encode_session(inst))
            region = MSM_VIDC_NON_SECURE_PIXEL;
        else
            region = MSM_VIDC_NON_SECURE;
        break;
    case MSM_VIDC_BUF_OUTPUT:
        if (is_encode_session(inst))
            region = MSM_VIDC_NON_SECURE;
        else
            region = MSM_VIDC_NON_SECURE_PIXEL;
        break;
    case MSM_VIDC_BUF_DPB:
    case MSM_VIDC_BUF_VPSS:
    case MSM_VIDC_BUF_PARTIAL_DATA:
        region = MSM_VIDC_NON_SECURE_PIXEL;
        break;
    case MSM_VIDC_BUF_INPUT_META:
    case MSM_VIDC_BUF_OUTPUT_META:
    case MSM_VIDC_BUF_BIN:
    case MSM_VIDC_BUF_COMV:
    case MSM_VIDC_BUF_NON_COMV:
    case MSM_VIDC_BUF_LINE:
    case MSM_VIDC_BUF_PERSIST:
        region = MSM_VIDC_NON_SECURE;
        break;
    default:
        i_vpr_e(inst, "%s: invalid driver buffer type %d\n",
            __func__, buffer_type);
    }

	return region;
}

inline const struct msm_vidc_memory_ops *get_mem_ops_ext(void)
{
	const struct msm_vidc_memory_ops *mem_ops = get_mem_ops();
	static struct msm_vidc_memory_ops mem_ops_ext;

	memcpy(&mem_ops_ext, mem_ops, sizeof(struct msm_vidc_memory_ops));
	mem_ops_ext.buffer_region     = msm_vidc_buffer_region_ext;

	return &mem_ops_ext;
}
