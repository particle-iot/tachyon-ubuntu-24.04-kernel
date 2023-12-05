// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define DRV_NAME "msm-stub-codec"

static struct snd_soc_dai_driver msm_stub_dais[] = {
	{
		.name = "msm-stub-rx",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
	},
	{
		.name = "msm-stub-tx",
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S24_3LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
	},
};

static const struct snd_soc_component_driver soc_msm_stub = {
	.name = DRV_NAME,
};

static int msm_stub_dev_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &soc_msm_stub, msm_stub_dais,
			ARRAY_SIZE(msm_stub_dais));
}

static const struct of_device_id msm_stub_codec_dt_match[] = {
	{.compatible = "qcom,msm-stub-codec", },
	{}
};
MODULE_DEVICE_TABLE(of, msm_stub_codec_dt_match);

static struct platform_driver msm_stub_driver = {
	.driver = {
		.name = "msm-stub-codec",
		.of_match_table = of_match_ptr(msm_stub_codec_dt_match),
	},
	.probe = msm_stub_dev_probe,
};

module_platform_driver(msm_stub_driver);

MODULE_DESCRIPTION("MSM STUB CODEC driver");
MODULE_LICENSE("GPL");
