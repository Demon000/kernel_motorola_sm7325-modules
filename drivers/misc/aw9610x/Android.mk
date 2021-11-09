DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)

ifeq ($(SX933X_USB_USE_ONLINE),true)
	KBUILD_OPTIONS += CONFIG_SX933X_POWER_SUPPLY_ONLINE=y
endif

include $(CLEAR_VARS)
LOCAL_MODULE := aw9610x.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_ADDITIONAL_DEPENDENCIES := $(KERNEL_MODULES_OUT)/sensors_class.ko
KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki
include $(DLKM_DIR)/AndroidKernelModule.mk