# oxdk.mk -- OXDK build system for cross-compiling MS XDK projects
#
# Include this file from your project Makefile after setting:
#   OXDK_DIR    - path to this OXDK directory
#   XDK_DIR     - path to installed XDK (e.g. copied from C:\Program Files\Microsoft Xbox SDK\xbox)
#   XDK_PRV_DIR - path to private Xbox tree (optional, for dashboard/internal builds)
#   SRCS        - list of .cpp source files
#   XBE_TITLE   - title string for the XBE
#   XBE_MODE    - DEBUG or RETAIL (default: RETAIL)

ifeq ($(OXDK_DIR),)
$(error OXDK_DIR must be set to the OXDK directory)
endif

ifeq ($(XDK_DIR),)
XDK_DIR = $(OXDK_DIR)/xdk
endif

# Verify the user has dropped XDK files in
ifeq ($(wildcard $(XDK_DIR)/lib/xboxkrnl.lib),)
$(error XDK libs not found. Copy your XDK lib/*.lib files into $(XDK_DIR)/lib/)
endif

ifeq ($(XBE_TITLE),)
XBE_TITLE = XDK App
endif

ifeq ($(XBE_MODE),)
XBE_MODE = RETAIL
endif

ifeq ($(OUTPUT_DIR),)
OUTPUT_DIR = bin
endif

CXBE = $(OXDK_DIR)/tools/cxbe/cxbe

# Compiler flags -- target Xbox Pentium III, MSVC 7.1 ABI compatibility
OXDK_TARGET_FLAGS = -target i386-pc-windows-msvc -march=pentium3 \
	-fms-extensions -fms-compatibility -fms-compatibility-version=13.10 \
	-fdelayed-template-parsing

OXDK_CFLAGS = $(OXDK_TARGET_FLAGS) -c \
	-D_XBOX -D_X86_ -DWIN32_LEAN_AND_MEAN \
	-Wno-microsoft-include -Wno-pragma-pack -Wno-ignored-pragmas \
	-Wno-deprecated-declarations -Wno-writable-strings -Wno-microsoft-cast \
	-Wno-unknown-pragmas -Wno-extra-tokens -Wno-nonportable-include-path \
	-Wno-typedef-redefinition \
	-include $(OXDK_DIR)/xdk_compat.h \
	-I$(OXDK_DIR) -I$(XDK_DIR)/include

# C++ flags -- MSVC /Gz (__stdcall default) is MANDATORY for XDK compatibility
OXDK_CXXFLAGS = $(OXDK_CFLAGS) -fno-rtti -fno-exceptions \
	-Xclang -fdefault-calling-conv=stdcall

# Linker flags
OXDK_LDFLAGS = /nologo /subsystem:windows /fixed:no /base:0x00010000 /stack:1048576 \
	/machine:x86 /entry:mainCRTStartup /nodefaultlib /force:multiple \
	/safeseh:no /merge:.edata=.edataxb /errorlimit:0 \
	/libpath:$(XDK_DIR)/lib

# Kernel import decorations -- clang generates undecorated names but
# xboxkrnl.lib uses stdcall-decorated (__imp__Name@N) symbols.
# Add /alternatename mappings for any kernel functions your project uses.
OXDK_KERNEL_IMPORTS = \
	/alternatename:__imp__HalReturnToFirmware=__imp__HalReturnToFirmware@4 \
	/alternatename:__imp__HalInitiateShutdown=__imp__HalInitiateShutdown@0 \
	/alternatename:__imp__HalReadSMCTrayState=__imp__HalReadSMCTrayState@8 \
	/alternatename:__imp__HalReadSMBusValue=__imp__HalReadSMBusValue@16 \
	/alternatename:__imp__HalWriteSMBusValue=__imp__HalWriteSMBusValue@16 \
	/alternatename:__imp__IoCreateSymbolicLink=__imp__IoCreateSymbolicLink@8 \
	/alternatename:__imp__IoDeleteSymbolicLink=__imp__IoDeleteSymbolicLink@4 \
	/alternatename:__imp__IoDismountVolumeByName=__imp__IoDismountVolumeByName@4 \
	/alternatename:__imp__MmFreeContiguousMemory=__imp__MmFreeContiguousMemory@4

# Default XDK libs (debug). Override OXDK_LIBS in your Makefile for release.
OXDK_LIBS = libcmtd.lib libcpmtd.lib xboxkrnl.lib \
	d3d8d.lib d3dx8d.lib xgraphicsd.lib dsoundd.lib \
	xnetd.lib xonlined.lib xbdm.lib \
	xapilibd.lib xapilib.lib xapilibp.lib

# Build rules
OBJS = $(addprefix $(OUTPUT_DIR)/,$(SRCS:.cpp=.obj))

.PHONY: all clean

all: $(OUTPUT_DIR)/default.xbe
	@echo "=== Build complete: $< ==="

$(OUTPUT_DIR)/default.xbe: $(OUTPUT_DIR)/$(XBE_TITLE).exe $(CXBE)
	$(CXBE) -MODE:$(XBE_MODE) -TITLE:"$(XBE_TITLE)" -OUT:$@ $<

$(OUTPUT_DIR)/$(XBE_TITLE).exe: $(OBJS)
	lld-link $(OXDK_LDFLAGS) $(OXDK_KERNEL_IMPORTS) $(LDFLAGS) \
		/map:$(OUTPUT_DIR)/$(XBE_TITLE).map /out:$@ $^ $(OXDK_LIBS) $(LIBS)

$(OUTPUT_DIR)/%.obj: %.cpp | $(OUTPUT_DIR)
	@mkdir -p $(dir $@)
	clang++ $(OXDK_CXXFLAGS) $(CXXFLAGS) -o $@ $<

$(OUTPUT_DIR)/%.obj: %.c | $(OUTPUT_DIR)
	@mkdir -p $(dir $@)
	clang $(OXDK_CFLAGS) $(CFLAGS) -o $@ $<

$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

$(CXBE):
	@echo "Building cxbe..."
	$(MAKE) -C $(OXDK_DIR)/tools/cxbe

clean:
	rm -rf $(OUTPUT_DIR)
