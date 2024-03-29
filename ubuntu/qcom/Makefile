subdir-ccflags-y += -Wno-packed-bitfield-compat
subdir-ccflags-y += -Wno-enum-int-mismatch

export MACHINE = qcm6490
export KERNEL_ROOT=/

export DISPLAY_ROOT := $(srctree)/$(src)/display

obj-y += display/

###

export KGSL_PATH := $(srctree)/$(src)/graphics
export KERNEL_SRC := $(srctree)
obj-y += graphics/

###

export CAMERA_ARCH=qcm6490
export SOC_FAM=qcm6490
export CAMERA_KERNEL_ROOT := $(srctree)/$(src)/camera

obj-y += camera/

###

obj-y += qps615/


###

export VIDEO_KERNEL_ROOT := $(srctree)/$(src)/video

obj-y += video/
