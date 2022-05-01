# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

LOCAL_PATH := $(call my-dir)

# -O3 # Try not to change the optimization/debugging level...
EXEC_FLAGS := -fPIE -fPIC -Wno-deprecated -std=c++11
EXEC_FLAGS += -W -Wall -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast -Werror=implicit-function-declaration -Wno-unused\
                -Winit-self -Wpointer-arith -Werror=return-type -Werror=non-virtual-dtor -Werror=address -Werror=sequence-point
LOCAL_C_INCLUDES += $(LOCAL_PATH)

ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    EXEC_FLAGS += -march=armv8-a+crc
endif

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    conf/cmdarg.cpp \
    conf/config.cpp \
    impl/cd9660.cpp \
    impl/device.cpp \
    impl/strdec.cpp \
    impl/strenc.cpp \
    impl/source.cpp \
    impl/unique.cpp \
    impl/rlimit.cpp \
    impl/burner.cpp \
    impl/extent.cpp \
    impl/vfat32.cpp \
    impl/datetm.cpp \
    impl/volume.cpp \
    impl/hfplus.cpp \
    impl/master.cpp \
    impl/mapper.cpp \
    impl/attrib.cpp

LOCAL_PCH := wrapper.h
LOCAL_CFLAGS += $(EXEC_FLAGS) -DPCH
LOCAL_MODULE := libfsview
LOCAL_THIN_ARCHIVE := true
LOCAL_MULTILIB := 64
include $(BUILD_STATIC_LIBRARY)

define fsview_exec
    include $(CLEAR_VARS)
    LOCAL_SRC_FILES := fsview_$1.cpp
    LOCAL_CFLAGS += $(EXEC_FLAGS)
    LOCAL_LDFLAGS += -fuse-ld=bfd $(NDK_USE_LD)
    # -Wl,--fix-cortex-a53-843419
    LOCAL_STATIC_LIBRARIES := libfsview
    LOCAL_MODULE := fsview_$1
    # The following assignments only affect the platform build:
    LOCAL_MULTILIB := 64
    LOCAL_MODULE_TAGS := userdebug eng
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
    LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)
    include $(BUILD_EXECUTABLE)
endef

FSVIEW_TOOLS := down fork hash mkfs name temp
$(foreach item,$(FSVIEW_TOOLS),$(eval $(call fsview_exec,$(item))))

include $(call all-makefiles-under,$(LOCAL_PATH))
