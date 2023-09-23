// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "iris_core.h"
#include "iris_helpers.h"
#include "iris_hfi.h"
#include "iris_hfi_queue.h"
#include "resources.h"
#include "iris_vidc.h"
#include "iris_ctrls.h"

static int init_iris_isr(struct iris_core *core)
{
	int ret;

	ret = devm_request_threaded_irq(core->dev, core->irq, iris_hfi_isr,
					iris_hfi_isr_handler, IRQF_TRIGGER_HIGH, "iris", core);
	if (ret) {
		dev_err(core->dev, "%s: Failed to allocate iris IRQ\n", __func__);
		return ret;
	}
	disable_irq_nosync(core->irq);

	return ret;
}

static void iris_unregister_video_device(struct iris_core *core)
{
	video_unregister_device(core->vdev_dec);
}

static int iris_register_video_device(struct iris_core *core)
{
	struct video_device *vdev;
	int ret;

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	strscpy(vdev->name, "qcom-iris-decoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = core->v4l2_file_ops;
	vdev->ioctl_ops = core->v4l2_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_vdev_release;

	core->vdev_dec = vdev;
	video_set_drvdata(vdev, core);

	return ret;

err_vdev_release:
	video_device_release(vdev);

	return ret;
}

static void iris_remove(struct platform_device *pdev)
{
	struct iris_core *core;

	core = platform_get_drvdata(pdev);
	if (!core)
		return;

	iris_core_deinit(core);
	iris_hfi_queue_deinit(core);

	iris_unregister_video_device(core);

	v4l2_device_unregister(&core->v4l2_dev);

	mutex_destroy(&core->lock);
	core->state = IRIS_CORE_DEINIT;
}

static int iris_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iris_core *core;
	u64 dma_mask;
	int ret;

	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = dev;

	core->state = IRIS_CORE_DEINIT;
	mutex_init(&core->lock);

	core->packet_size = IFACEQ_CORE_PKT_SIZE;
	core->packet = devm_kzalloc(core->dev, core->packet_size, GFP_KERNEL);
	if (!core->packet)
		return -ENOMEM;

	core->response_packet = devm_kzalloc(core->dev, core->packet_size, GFP_KERNEL);
	if (!core->response_packet)
		return -ENOMEM;

	INIT_LIST_HEAD(&core->instances);

	core->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->reg_base))
		return PTR_ERR(core->reg_base);

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0)
		return core->irq;

	ret = init_iris_isr(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: Failed to init isr with %d\n", __func__, ret);
		return ret;
	}

	ret = init_platform(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init platform failed with %d\n", __func__, ret);
		return ret;
	}

	ret = init_vpu(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init vpu failed with %d\n", __func__, ret);
		return ret;
	}

	ret = init_ops(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init ops failed with %d\n", __func__, ret);
		return ret;
	}

	ret = init_resources(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init resource failed with %d\n", __func__, ret);
		return ret;
	}

	ret = iris_init_core_caps(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init core caps failed with %d\n", __func__, ret);
		return ret;
	}

	ret = iris_init_instance_caps(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: init inst caps failed with %d\n", __func__, ret);
		return ret;
	}

	ret = v4l2_device_register(dev, &core->v4l2_dev);
	if (ret)
		return ret;

	ret = iris_register_video_device(core);
	if (ret)
		goto err_v4l2_unreg;

	platform_set_drvdata(pdev, core);

	dma_mask = core->cap[DMA_MASK].value;

	ret = dma_set_mask_and_coherent(dev, dma_mask);
	if (ret)
		goto err_vdev_unreg;

	dma_set_max_seg_size(&pdev->dev, (unsigned int)DMA_BIT_MASK(32));
	dma_set_seg_boundary(&pdev->dev, (unsigned long)DMA_BIT_MASK(64));

	ret = iris_hfi_queue_init(core);
	if (ret) {
		dev_err_probe(core->dev, ret,
			      "%s: interface queues init failed\n", __func__);
		goto err_vdev_unreg;
	}

	ret = iris_core_init(core);
	if (ret) {
		dev_err_probe(core->dev, ret, "%s: core init failed\n", __func__);
		goto err_queue_deinit;
	}

	return ret;

err_queue_deinit:
	iris_hfi_queue_deinit(core);
err_vdev_unreg:
	iris_unregister_video_device(core);
err_v4l2_unreg:
	v4l2_device_unregister(&core->v4l2_dev);

	return ret;
}

static const struct of_device_id iris_dt_match[] = {
	{
		.compatible = "qcom,sm8550-iris",
		.data = &sm8550_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, iris_dt_match);

static struct platform_driver qcom_iris_driver = {
	.probe = iris_probe,
	.remove_new = iris_remove,
	.driver = {
		.name = "qcom-iris",
		.of_match_table = iris_dt_match,
	},
};

module_platform_driver(qcom_iris_driver);
MODULE_IMPORT_NS(DMA_BUF);
MODULE_DESCRIPTION("Qualcomm Iris video driver");
MODULE_LICENSE("GPL");
