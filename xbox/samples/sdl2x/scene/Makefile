# Scene demo: RXDK-SDL2x video/input + modern C++17 (libc++) in one build.

OXDK_DIR    = ../../../..
XBE_TITLE   = sdl2_scene
XBE_NAME    = OXDK Summer 2026 Demo
XBE_MODE    = RETAIL
OXDK_LIBCXX = 1

include $(OXDK_DIR)/xbox/third-party/SDL2x/oxdk/sdl2x.mk

SRCS  = scene.cpp
SRCS += $(OXDK_DIR)/xbox/oxdk/libcxx-shim/libcxx_runtime.cpp
SRCS += $(SDL2X_SRC_LIST)

# At ms-compat 19.00 (libc++ mode) SDL_endian.h tries to define _m_prefetch,
# which clang already provides as a builtin. Setting SDL's own include guard
# makes it skip that and fall back to the builtin.
SCENE_FLAGS = -D__PRFCHWINTRIN_H

CFLAGS   += $(SDL2X_DEFINES) $(SDL2X_INCLUDES) $(SCENE_FLAGS)
CXXFLAGS += $(SDL2X_DEFINES) $(SDL2X_INCLUDES) $(SCENE_FLAGS)

OXDK_LIBS = $(SDL2X_LIBS)

include $(OXDK_DIR)/oxdk.mk
