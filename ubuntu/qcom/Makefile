subdir-ccflags-y += -Wno-packed-bitfield-compat

export MACHINE='qcm6490'
export CONFIG_QCA_CLD_WLAN=m
export CONFIG_CNSS_OUT_OF_TREE=y
export CONFIG_ICNSS2=m
export CONFIG_CNSS_QMI_SVC=m
export CONFIG_CNSS_PLAT_IPC_QMI_SVC=m
export CONFIG_WLAN_TX_MON_2_0=n
export CONFIG_WLAN_DP_LOCAL_PKT_CAPTURE=n
export CONFIG_CNSS_GENL=m
export CONFIG_LITHIUM=y
export CONFIG_CNSS_QCA6750=y

export MODNAME := wlan
export WLAN_ROOT := $(srctree)/$(src)/qcacld-3.0
export CONFIG_QCA_CLD_WLAN=m
export CONFIG_QCA_WIFI_ISOC=0
export CONFIG_QCA_WIFI_2_0=1

obj-y += qcacld-3.0/

###

export WLAN_PLATFORM_ROOT := $(srctree)/$(src)/platform
export KERNEL_ROOT=/
export CONFIG_ICNSS2=m
export CONFIG_ICNSS2_QMI=y
export CONFIG_ICNSS2_DEBUG=y
export CONFIG_CNSS_QMI_SVC=m
export CONFIG_WCNSS_MEM_PRE_ALLOC=m
export CONFIG_CNSS_UTILS=m
export CONFIG_CNSS_GENL=m
export CONFIG_CNSS_PLAT_IPC_QMI_SVC=m

obj-y += platform/

###

export DISPLAY_ROOT := $(srctree)/$(src)/display

obj-y += display/

###

export KGSL_PATH := $(srctree)/$(src)/graphics
export KERNEL_SRC := $(srctree)
obj-y += graphics/

###

export CAMERA_ARCH=qcm6490
export CAMERA_KERNEL_ROOT := $(srctree)/$(src)/camera

obj-y += camera/

###

export BT_ROOT := $(srctree)/$(src)/bt

export CONFIG_BTFM_SLIM=m

obj-y += bt/

###

obj-y += qps615/
