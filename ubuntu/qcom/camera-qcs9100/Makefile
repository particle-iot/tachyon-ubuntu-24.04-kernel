CAMERA_ARCH=qcs9100
SOC_FAM=qcs9100
CAMERA_KERNEL_ROOT := $(srctree)/$(src)/../camera

include $(srctree)/$(src)/../camera/Kbuild

objs := $(camera_$(CAMERA_ARCH)-y) $(camera_$(CAMERA_ARCH)-m)

srcs := $(objs:%.o=%.c)

$(addprefix $(obj)/, $(srcs)): $(obj)/%.c: $(src)/../camera/%.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed,copy)

targets += $(srcs)
