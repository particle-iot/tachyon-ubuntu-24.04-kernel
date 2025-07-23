CAMERA_ARCH=qcm6490
SOC_FAM=qcm6490
CAMERA_KERNEL_ROOT := $(srctree)/$(src)/../camera

include $(srctree)/$(src)/../camera/Kbuild

objs := $(camera_$(CAMERA_ARCH)-y) $(camera_$(CAMERA_ARCH)-m)

srcs := $(objs:%.o=%.c)

$(addprefix $(obj)/, $(srcs)): $(obj)/%.c: $(src)/../camera/%.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed,copy)

targets += $(srcs)
