# Check the Xenon ABI against Microsoft's own compiled code. Needs a clang carrying
# the xenon-abi feature; see tools/abitest/abicheck.py.
OXDK_DIR    := ../../../..
OXDK_TARGET := xbox360
SRCS        := main.c probe.S
XEX_TITLE   := abi
XENON_ABI   := 1
include $(OXDK_DIR)/oxdk.mk
