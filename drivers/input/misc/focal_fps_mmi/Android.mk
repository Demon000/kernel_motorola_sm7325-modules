DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)

ifeq ($(DLKM_INSTALL_TO_VENDOR_OUT),true)
FOCAL_FPS_MMI_MODULE_PATH := $(TARGET_OUT_VENDOR)/lib/modules/
else
FOCAL_FPS_MMI_MODULE_PATH := $(KERNEL_MODULES_OUT)
endif

ifeq ($(FOCAL_SUPPORT_DRM_SCREEN),true)
	KBUILD_OPTIONS += CONFIG_FOCAL_SUPPORT_DRM_SCREEN=y
endif

ifeq ($(FOCAL_FPS_PANEL_NOTIFICATIONS),true)
    KERNEL_CFLAGS += CONFIG_FOCAL_PANEL_NOTIFICATIONS=y
endif

include $(CLEAR_VARS)
LOCAL_MODULE := focal_fps_mmi.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(FOCAL_FPS_MMI_MODULE_PATH)
include $(DLKM_DIR)/AndroidKernelModule.mk

