CAMERA_ARCH=qcs9100
SOC_FAM=qcs9100
CAMERA_KERNEL_ROOT := $(srctree)/$(src)/../camera

include $(srctree)/$(src)/../camera/Kbuild

obj-m := camera-qcs9100.o

camera-qcs9100-objs := $(camera-y) $(camera-m)

srcs := $(camera-qcs9100-objs:%.o=%.c)

$(addprefix $(obj)/, $(srcs)): $(obj)/%.c: $(src)/../camera/%.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed,copy)

targets += $(srcs)
