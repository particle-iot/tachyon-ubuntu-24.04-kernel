# SPDX-License-Identifier: GPL-2.0-only
BT_ROOT = $(shell pwd)
all: modules

# If it reaches here, compilation is probably without Android.mk,
# so enable all flags (including debug flag CONFIG_CNSS2_DEBUG) by
# default.
KBUILD_OPTIONS += BT_ROOT=$(BT_ROOT)
#KBUILD_OPTIONS += KERNEL_ROOT=$(ROOT_DIR)/$(KERNEL_DIR)
#KBUILD_OPTIONS += CONFIG_MSM_BT_POWER=m
KBUILD_OPTIONS += CONFIG_BTFM_SLIM=m

$(info OPTIONSSS-K2C: $(KBUILD_OPTIONS))

$(info OPTIONSSS-K2C: $(KERNEL_SRC))
SRC := $(shell pwd)

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) $(KBUILD_OPTIONS) clean

headers_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) headers_install $(KBUILD_OPTIONS)

