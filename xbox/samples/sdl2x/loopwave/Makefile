# RXDK-SDL2x loopwave sample on OXDK.
#
# Source: ../../../third-party/SDL2x/Samples/loopwave/loopwave.cpp (pristine upstream).

OXDK_DIR  = ../../../..
XBE_TITLE = sdl2_loopwave
XBE_MODE  = RETAIL

include $(OXDK_DIR)/xbox/third-party/SDL2x/oxdk/sdl2x.mk

SRCS  = $(SDL2X_ROOT)/Samples/loopwave/loopwave.cpp
SRCS += $(SDL2X_SRC_LIST)

CFLAGS   += $(SDL2X_DEFINES) $(SDL2X_INCLUDES)
CXXFLAGS += $(SDL2X_DEFINES) $(SDL2X_INCLUDES)

OXDK_LIBS = $(SDL2X_LIBS)

include $(OXDK_DIR)/oxdk.mk
