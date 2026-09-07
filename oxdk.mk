# OXDK, the front door.
#
# Include this from a project Makefile after setting OXDK_DIR to this
# directory. Pick a console with OXDK_TARGET:
#
#   OXDK_TARGET = xbox      original Xbox, builds an XBE   (default)
#   OXDK_TARGET = xbox360   Xbox 360, builds a XEX         (also spelled xenon)
#
# Everything else is documented by the console you chose, in xbox/xbox.mk or
# xbox360/xbox360.mk.

ifeq ($(OXDK_DIR),)
$(error OXDK_DIR must be set to the OXDK directory)
endif

OXDK_ROOT := $(OXDK_DIR)

OXDK_TARGET ?= xbox
ifeq ($(OXDK_TARGET),xenon)
OXDK_TARGET := xbox360
endif

include $(OXDK_ROOT)/common/toolchain.mk

ifeq ($(OXDK_TARGET),xbox)
OXDK_XBOX_DIR := $(OXDK_ROOT)/xbox
include $(OXDK_ROOT)/xbox/xbox.mk
else ifeq ($(OXDK_TARGET),xbox360)
OXDK360_DIR := $(OXDK_ROOT)/xbox360
include $(OXDK_ROOT)/xbox360/xbox360.mk
else
$(error OXDK_TARGET must be xbox or xbox360, not "$(OXDK_TARGET)")
endif
