// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>

#include "vpu_iris3.h"
#include "vpu_iris3_buffer.h"

#define VIDEO_ARCH_LX 1

#define CPU_BASE_OFFS_IRIS3     0x000A0000

#define CPU_CS_BASE_OFFS_IRIS3      (CPU_BASE_OFFS_IRIS3)
#define CPU_IC_BASE_OFFS_IRIS3      (CPU_BASE_OFFS_IRIS3)

#define CPU_CS_VCICMDARG0_IRIS3     (CPU_CS_BASE_OFFS_IRIS3 + 0x24)
#define CPU_CS_VCICMDARG1_IRIS3     (CPU_CS_BASE_OFFS_IRIS3 + 0x28)

#define CPU_CS_A2HSOFTINTCLR_IRIS3  (CPU_CS_BASE_OFFS_IRIS3 + 0x1C)

/* HFI_CTRL_INIT */
#define CPU_CS_SCIACMD_IRIS3        (CPU_CS_BASE_OFFS_IRIS3 + 0x48)

/* HFI_CTRL_STATUS */
#define CPU_CS_SCIACMDARG0_IRIS3    (CPU_CS_BASE_OFFS_IRIS3 + 0x4C)
#define CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS3     0x40000000

#define CPU_CS_H2XSOFTINTEN_IRIS3   (CPU_CS_BASE_OFFS_IRIS3 + 0x148)

#define CPU_CS_X2RPMH_IRIS3         (CPU_CS_BASE_OFFS_IRIS3 + 0x168)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG1_IRIS3       (CPU_CS_BASE_OFFS_IRIS3 + 0x64)

/* UC_REGION_ADDR */
#define CPU_CS_SCIBARG2_IRIS3       (CPU_CS_BASE_OFFS_IRIS3 + 0x68)

/* HFI_QTBL_INFO */
#define CPU_CS_SCIACMDARG1_IRIS3    (CPU_CS_BASE_OFFS_IRIS3 + 0x50)

/* HFI_QTBL_ADDR */
#define CPU_CS_SCIACMDARG2_IRIS3    (CPU_CS_BASE_OFFS_IRIS3 + 0x54)

/* SFR_ADDR */
#define CPU_CS_SCIBCMD_IRIS3        (CPU_CS_BASE_OFFS_IRIS3 + 0x5C)

#define UC_REGION_ADDR_IRIS3        CPU_CS_SCIBARG1_IRIS3
#define UC_REGION_SIZE_IRIS3	    CPU_CS_SCIBARG2_IRIS3

#define QTBL_INFO_IRIS3             CPU_CS_SCIACMDARG1_IRIS3
#define QTBL_ADDR_IRIS3             CPU_CS_SCIACMDARG2_IRIS3

#define SFR_ADDR_IRIS3              CPU_CS_SCIBCMD_IRIS3

#define CTRL_INIT_IRIS3             CPU_CS_SCIACMD_IRIS3

#define CTRL_STATUS_IRIS3           CPU_CS_SCIACMDARG0_IRIS3
#define CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS3	0xfe
#define CTRL_ERROR_STATUS__M_IRIS3 \
		CPU_CS_SCIACMDARG0_HFI_CTRL_ERROR_STATUS_BMSK_IRIS3
#define CTRL_INIT_IDLE_MSG_BMSK_IRIS3 \
		CPU_CS_SCIACMDARG0_HFI_CTRL_INIT_IDLE_MSG_BMSK_IRIS3

#define WRAPPER_BASE_OFFS_IRIS3		         0x000B0000
#define WRAPPER_INTR_STATUS_IRIS3	         (WRAPPER_BASE_OFFS_IRIS3 + 0x0C)
#define WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3  0x8
#define WRAPPER_INTR_STATUS_A2H_BMSK_IRIS3	  0x4

#define CPU_IC_SOFTINT_IRIS3        (CPU_IC_BASE_OFFS_IRIS3 + 0x150)
#define CPU_IC_SOFTINT_H2A_SHFT_IRIS3	0x0

#define WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3  0x8

static int setup_ucregion_memory_map_iris3(struct iris_core *core)
{
	int ret;
	u32 value;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, UC_REGION_ADDR_IRIS3, value);
	if (ret)
		return ret;

	value = SHARED_QSIZE;
	ret = write_register(core, UC_REGION_SIZE_IRIS3, value);
	if (ret)
		return ret;

	value = (u32)core->iface_q_table.device_addr;
	ret = write_register(core, QTBL_ADDR_IRIS3, value);
	if (ret)
		return ret;

	ret = write_register(core, QTBL_INFO_IRIS3, 0x01);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr);
	ret = write_register(core, CPU_CS_VCICMDARG0_IRIS3, value);
	if (ret)
		return ret;

	value = (u32)((u64)core->iface_q_table.kernel_vaddr >> 32);
	ret = write_register(core, CPU_CS_VCICMDARG1_IRIS3, value);
	if (ret)
		return ret;

	if (core->sfr.device_addr) {
		value = (u32)core->sfr.device_addr + VIDEO_ARCH_LX;
		ret = write_register(core, SFR_ADDR_IRIS3, value);
		if (ret)
			return ret;
	}

	return ret;
}

static int boot_firmware_iris3(struct iris_core *core)
{
	u32 ctrl_init = 0, ctrl_status = 0, count = 0, max_tries = 1000;
	int ret;

	ret = setup_ucregion_memory_map_iris3(core);
	if (ret)
		return ret;

	ctrl_init = BIT(0);

	ret = write_register(core, CTRL_INIT_IRIS3, ctrl_init);
	if (ret)
		return ret;

	while (!ctrl_status && count < max_tries) {
		ret = read_register(core, CTRL_STATUS_IRIS3, &ctrl_status);
		if (ret)
			return ret;

		if ((ctrl_status & CTRL_ERROR_STATUS__M_IRIS3) == 0x4) {
			dev_err(core->dev, "invalid setting for UC_REGION\n");
			break;
		}

		usleep_range(50, 100);
		count++;
	}

	if (count >= max_tries) {
		dev_err(core->dev, "Error booting up vidc firmware\n");
		return -ETIME;
	}

	ret = write_register(core, CPU_CS_H2XSOFTINTEN_IRIS3, 0x1);
	if (ret)
		return ret;

	ret = write_register(core, CPU_CS_X2RPMH_IRIS3, 0x0);

	return ret;
}

static int raise_interrupt_iris3(struct iris_core *core)
{
	return write_register(core, CPU_IC_SOFTINT_IRIS3, 1 << CPU_IC_SOFTINT_H2A_SHFT_IRIS3);
}

static int clear_interrupt_iris3(struct iris_core *core)
{
	u32 intr_status = 0, mask = 0;
	int ret;

	ret = read_register(core, WRAPPER_INTR_STATUS_IRIS3, &intr_status);
	if (ret)
		return ret;

	mask = (WRAPPER_INTR_STATUS_A2H_BMSK_IRIS3 |
		WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3 |
		CTRL_INIT_IDLE_MSG_BMSK_IRIS3);

	if (intr_status & mask) {
		core->intr_status |= intr_status;
		core->reg_count++;
	} else {
		core->spur_count++;
	}

	ret = write_register(core, CPU_CS_A2HSOFTINTCLR_IRIS3, 1);

	return ret;
}

static int watchdog_iris3(struct iris_core *core, u32 intr_status)
{
	if (intr_status & WRAPPER_INTR_STATUS_A2HWD_BMSK_IRIS3) {
		dev_err(core->dev, "%s: received watchdog interrupt\n", __func__);
		return -ETIME;
	}

	return 0;
}

static const struct vpu_ops iris3_ops = {
	.boot_firmware = boot_firmware_iris3,
	.raise_interrupt = raise_interrupt_iris3,
	.clear_interrupt = clear_interrupt_iris3,
	.watchdog = watchdog_iris3,
};

static const struct vpu_session_ops iris3_session_ops = {
	.int_buf_size = iris_int_buf_size_iris3,
};

int init_iris3(struct iris_core *core)
{
	core->vpu_ops = &iris3_ops;
	core->session_ops = &iris3_session_ops;

	return 0;
}
