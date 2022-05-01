LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        $(call all-java-files-under, src)

# the component is extremely thin,
# so neither resources nor ProGuard are needed
LOCAL_PROGUARD_ENABLED := disabled

LOCAL_PACKAGE_NAME := UsbSignalingService
LOCAL_CERTIFICATE := platform
LOCAL_PRIVILEGED_MODULE := true
# FIXME: allow in user builds
LOCAL_MODULE_TAGS := userdebug eng

include $(BUILD_PACKAGE)
