# SPDX-License-Identifier: GPL-2.0-only

M := $(shell pwd)
KBUILD_OPTIONS += VIDEO_KERNEL_ROOT=$(M)

VIDEO_COMPILE_TIME = $(shell date)
VIDEO_COMPILE_HOST = $(shell uname -n)

vidc_generated_h: $(shell find . -iname "*.c") $(shell find . -iname "*.h") $(shell find . -iname "*.mk")
	echo '#define VIDEO_COMPILE_TIME "$(VIDEO_COMPILE_TIME)"' > vidc_generated_h
	echo '#define VIDEO_COMPILE_HOST "$(VIDEO_COMPILE_HOST)"' >> vidc_generated_h

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules: vidc_generated_h

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)

clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd
	rm -rf .tmp_versions
