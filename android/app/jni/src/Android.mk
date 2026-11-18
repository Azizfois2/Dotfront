LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := main
SDL_PATH := ../SDL
ROOT := $(LOCAL_PATH)/../../../..
LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include $(ROOT)/src
LOCAL_SRC_FILES := \
    $(ROOT)/src/main.c $(ROOT)/src/entry.c $(ROOT)/src/gfx.c \
    $(ROOT)/src/font.c $(ROOT)/src/world.c $(ROOT)/src/game.c \
    $(ROOT)/src/units.c $(ROOT)/src/ai.c $(ROOT)/src/render.c \
    $(ROOT)/src/hud.c $(ROOT)/src/net.c $(ROOT)/src/platform_sdl.c
LOCAL_CFLAGS := -std=c11 -O2 -DWOD_TOUCH=1 -Wno-unused-parameter
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid -lm
include $(BUILD_SHARED_LIBRARY)
