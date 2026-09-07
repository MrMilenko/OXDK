# Minimal C++17 + libc++ sample, proving modern C++ links to an XBE on OXDK.

OXDK_DIR    = ../../../..
XBE_TITLE   = cxx17_hello
XBE_MODE    = RETAIL
OXDK_LIBCXX = 1

SRCS = main.cpp $(OXDK_DIR)/xbox/oxdk/libcxx-shim/libcxx_runtime.cpp

# Just the CRT + kernel; no graphics/audio needed for this proof.
OXDK_LIBS = libcmt.lib libcpmt.lib xboxkrnl.lib xapilib.lib

include $(OXDK_DIR)/oxdk.mk
