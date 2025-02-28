CAMERA_ARCH=qcm6490
SOC_FAM=qcm6490
CAMERA_KERNEL_ROOT := $(srctree)/$(src)/../camera

include $(srctree)/$(src)/../camera/Kbuild

obj-m := camera-qcm6490.o

camera-qcm6490-objs := $(camera-y) $(camera-m)

srcs := $(camera-qcm6490-objs:%.o=%.c)

$(addprefix $(obj)/, $(srcs)): $(obj)/%.c: $(src)/../camera/%.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed,copy)

targets += $(srcs)
