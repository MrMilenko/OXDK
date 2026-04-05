# Example OXDK project -- minimal D3D app
#
# Usage:
#   1. Make sure your XDK libs/headers are in ../../xdk/
#   2. Run: make
#   3. Copy bin/default.xbe to your Xbox

OXDK_DIR  = ../..
XBE_TITLE = Hello
XBE_MODE  = RETAIL

SRCS = main.cpp lld_workaround.cpp

# Don't need private libs for this example
OXDK_LIBS = libcmtd.lib libcpmtd.lib xboxkrnl.lib \
	d3d8d.lib d3dx8d.lib xgraphicsd.lib dsoundd.lib \
	xnetd.lib xonlined.lib \
	xbdm.lib xapilibd.lib xapilib.lib

include $(OXDK_DIR)/oxdk.mk
