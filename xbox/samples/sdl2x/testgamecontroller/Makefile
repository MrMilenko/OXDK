# RXDK-SDL2x testgamecontroller sample on OXDK.
#
# Source: ../../../third-party/SDL2x/Samples/testgamecontroller/ (pristine upstream).
# Two source files: the test app itself plus its shared testutils helper.

OXDK_DIR  = ../../../..
XBE_TITLE = sdl2_testgamecontroller
XBE_MODE  = RETAIL

include $(OXDK_DIR)/xbox/third-party/SDL2x/oxdk/sdl2x.mk

SRCS  = $(SDL2X_ROOT)/Samples/testgamecontroller/testgamecontroller.cpp
SRCS += $(SDL2X_ROOT)/Samples/testgamecontroller/testutils.c
SRCS += $(SDL2X_SRC_LIST)

CFLAGS   += $(SDL2X_DEFINES) $(SDL2X_INCLUDES)
CXXFLAGS += $(SDL2X_DEFINES) $(SDL2X_INCLUDES)

OXDK_LIBS = $(SDL2X_LIBS)

include $(OXDK_DIR)/oxdk.mk
