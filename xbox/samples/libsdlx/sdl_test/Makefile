# OXDK + libSDLx smoke test.
#
# Builds libSDLx (vendored at ../../../third-party/libSDLx) together with a
# tiny SDL app that fills a 24bpp surface and blits it to a 16bpp screen.
# If the resulting bin/default.xbe shows 8 vertical color bars for 5
# seconds and returns to dash, your OXDK + libSDLx setup is working.

OXDK_DIR    = ../../../..
LIBSDLX_DIR = $(OXDK_DIR)/xbox/third-party/libSDLx

XBE_TITLE   = sdl_test
XBE_MODE    = RETAIL

# App + platform shim sources.
SRCS = sdl_test.cpp xbox_platform.c

# Pull in libSDLx core .c files. Exclude SIMD asm paths (not built) and
# the cdrom code (its undocumented.h header conflicts; xbox_platform.c
# stubs out SDL_CDROMInit/Quit so the link is satisfied).
SRCS += $(filter-out \
        $(LIBSDLX_DIR)/SDL/src/audio/SDL_mixer_MMX.c \
        $(LIBSDLX_DIR)/SDL/src/audio/SDL_mixer_MMX_VC.c \
        $(LIBSDLX_DIR)/SDL/src/audio/SDL_mixer_m68k.c \
        $(LIBSDLX_DIR)/SDL/src/video/SDL_yuv_mmx.c \
        $(LIBSDLX_DIR)/SDL/src/cdrom/SDL_cdrom.c \
        $(LIBSDLX_DIR)/SDL/src/cdrom/xbox/SDL_syscdrom.c \
    , $(shell find $(LIBSDLX_DIR)/SDL/src -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/audio -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/audio/xbox -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/cpuinfo -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/endian -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/events -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/file -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/joystick -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/joystick/xbox -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/stdlib -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/thread -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/thread/xbox -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/timer -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/timer/xbox -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/video -maxdepth 1 -name '*.c'; \
              find $(LIBSDLX_DIR)/SDL/src/video/xbox -name '*.c'))

# libSDLx wants several defines and its own include dirs. These flow into
# both CFLAGS and CXXFLAGS so libSDLx .c files compile and the sample's
# C++ can include <SDL.h>.
#
# DEBUG_KEYBOARD and DEBUG_MOUSE pull in the public XDK XKbd.h /
# XMouse.h types that libSDLx's xbox/SDL_xboxevents.c declares for its
# TranslateKey forward decl. (Setting _XBOX_DONT_USE_DEVICES instead is
# tempting but libSDLx's guards around that define are inconsistent and
# the file fails to compile.) These are harmless on retail Xbox without
# a debug keyboard plugged in; the input code just stays inert.
SDL_DEFINES = -D_LIB -D_WINDOWS -DWIN32 \
              -DWAV_MUSIC -DENABLE_DIRECTX -DUSE_RWOPS -DNO_SIGNAL_H \
              -DDEBUG_KEYBOARD -DDEBUG_MOUSE

SDL_INCLUDES = -isystem $(LIBSDLX_DIR)/SDL/include \
               -I$(LIBSDLX_DIR)/SDL/src \
               -I$(LIBSDLX_DIR)/SDL/src/audio \
               -I$(LIBSDLX_DIR)/SDL/src/video \
               -I$(LIBSDLX_DIR)/SDL/src/joystick \
               -I$(LIBSDLX_DIR)/SDL/src/cdrom \
               -I$(LIBSDLX_DIR)/SDL/src/events \
               -I$(LIBSDLX_DIR)/SDL/src/thread \
               -I$(LIBSDLX_DIR)/SDL/src/timer

CFLAGS   += $(SDL_DEFINES) $(SDL_INCLUDES)
CXXFLAGS += $(SDL_DEFINES) $(SDL_INCLUDES)

# Retail XDK libs. The sample uses D3D + DSound (libSDLx pulls those in)
# but no networking, no XAPI perf counters.
OXDK_LIBS = libcmt.lib libcpmt.lib xboxkrnl.lib \
            d3d8.lib d3dx8.lib xgraphics.lib dsound.lib \
            xapilib.lib xkbd.lib

include $(OXDK_DIR)/oxdk.mk
