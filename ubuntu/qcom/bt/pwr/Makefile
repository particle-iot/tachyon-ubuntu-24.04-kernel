# SPDX-License-Identifier: GPL-2.0

KBUILD_OPTIONS+= BT_ROOT=$(KERNEL_SRC)/$(M)/../

ccflags-y += -I$(BT_ROOT)/include
# ccflags-y += -I$(WORKSPACE)/wlan/platform/inc
# ccflags-y += -I$(WORKSPACE)/wlan/platform/cnss_utils
obj-$(CONFIG_MSM_BT_POWER)  += btpower.o

all: modules

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)

clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd Module.symvers
	rm -rf .tmp_versions

