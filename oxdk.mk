# oxdk.mk -- OXDK build system for cross-compiling MS XDK projects
#
# Include this file from your project Makefile after setting:
#   OXDK_DIR    - path to this OXDK directory
#   XDK_DIR     - path to installed XDK (e.g. copied from C:\Program Files\Microsoft Xbox SDK\xbox)
#   XDK_PRV_DIR - path to private Xbox tree (optional, for dashboard/internal builds)
#   SRCS        - list of source files (.c, .cc, .cpp)
#   XBE_TITLE   - title string for the XBE
#   XBE_MODE    - DEBUG or RETAIL (default: RETAIL)
#   XBE_LIMIT64MB - yes or no, default yes. Set to no to let the title use all
#                 128 MB on an upgraded console or a devkit. Stock hardware has
#                 64 MB either way, so no is safe there too.

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

# XBE_TITLE doubles as the output filename base, so it stays space free.
# XBE_NAME is the title shown on the dashboard and may contain spaces.
ifeq ($(XBE_NAME),)
XBE_NAME = $(XBE_TITLE)
endif

ifeq ($(XBE_MODE),)
XBE_MODE = RETAIL
endif

ifeq ($(XBE_LIMIT64MB),)
XBE_LIMIT64MB = yes
endif

ifeq ($(OUTPUT_DIR),)
OUTPUT_DIR = bin
endif

CXBE = $(OXDK_DIR)/tools/cxbe/cxbe

# Modern C++ stdlib (libc++) is opt-in via OXDK_LIBCXX=1. The XDK's own STL is
# C++98, so C++11+ apps need libc++. That requires a newer MSVC-compat version
# (char16_t/char32_t became keywords at 19.00) and the libc++ headers ahead of
# the XDK's C++-ified C headers on the include path. Default mode is untouched.
OXDK_MSC_VER ?= 13.10
ifeq ($(OXDK_LIBCXX),1)
OXDK_MSC_VER := 19.00
OXDK_LIBCXX_DIR ?= /opt/homebrew/opt/llvm/include/c++/v1
OXDK_CLANG_RES  := $(shell clang -print-resource-dir)/include
# Our __config_site (threads/filesystem/localization off) wins for
# <__config_site>, then libc++ proper, then the clean-C-header shims, then
# clang builtins, then the XDK headers (as -isystem so they lose to libc++).
OXDK_LIBCXX_INC = -nostdinc++ \
	-isystem $(OXDK_DIR)/oxdk/libcxx-config \
	-isystem $(OXDK_LIBCXX_DIR) \
	-isystem $(OXDK_DIR)/oxdk/libcxx-cshim \
	-isystem $(OXDK_CLANG_RES)
# C files also benefit from the cshim (snprintf etc. the XDK CRT lacks).
OXDK_LIBCXX_C_INC = -isystem $(OXDK_DIR)/oxdk/libcxx-cshim
# clang emits the v3 C++ EH personality (__CxxFrameHandler3) which the XDK
# CRT does not ship, and enabling exceptions pulls in std::exception /
# std::logic_error / std::bad_array_new_length / __std_terminate that need
# real class bodies (libc++abi territory). Stay with -fno-exceptions for
# now; consumers that need try/catch will need an exception runtime first.
# NOMINMAX stops <windows.h> / xtl.h from defining min/max as macros that
# clobber std::min and std::max.
OXDK_LIBCXX_CXX = -fno-exceptions -DNOMINMAX
OXDK_XDK_INC = -isystem $(XDK_DIR)/include
else
OXDK_XDK_INC = -I$(XDK_DIR)/include
endif

# Target the Xbox Pentium III with MSVC ABI compatibility.
OXDK_TARGET_FLAGS = -target i386-pc-windows-msvc -march=pentium3 \
	-fms-extensions -fms-compatibility -fms-compatibility-version=$(OXDK_MSC_VER) \
	-fdelayed-template-parsing

# stdcall default mirrors MSVC /Gz so .c and .cpp emit matching decorated
# symbols. Required for kernel imports (@N-decorated).
OXDK_COMMON_FLAGS = -D_XBOX -D_X86_ -DWIN32_LEAN_AND_MEAN -D_NTOS_ -D_MT \
	-Wno-microsoft-include -Wno-pragma-pack -Wno-ignored-pragmas \
	-Wno-deprecated-declarations -Wno-writable-strings -Wno-microsoft-cast \
	-Wno-unknown-pragmas -Wno-extra-tokens -Wno-nonportable-include-path \
	-Wno-typedef-redefinition \
	-Xclang -fdefault-calling-conv=stdcall

OXDK_CFLAGS = $(OXDK_TARGET_FLAGS) -c $(OXDK_COMMON_FLAGS) \
	$(OXDK_LIBCXX_C_INC) -I$(OXDK_DIR) $(OXDK_XDK_INC)

# RTTI is on: libcmt ships __RTDynamicCast / __RTtypeid / __RTCastToVoid so
# dynamic_cast and typeid resolve through the MSVC runtime, and libc++'s
# own type_info out-of-line methods (__compare / ~type_info / name() /
# hash_code()) are provided in libcxx_runtime.cpp.
# -fno-threadsafe-statics: C++11 magic statics need __Init_thread_* helpers
# the XDK CRT doesn't ship; Xbox is single-threaded for static init anyway.
OXDK_CXXFLAGS = $(OXDK_TARGET_FLAGS) -c $(OXDK_COMMON_FLAGS) \
	-fno-threadsafe-statics \
	$(OXDK_LIBCXX_CXX) $(OXDK_LIBCXX_INC) -I$(OXDK_DIR) $(OXDK_XDK_INC)

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

# clang-emitted CRT helpers. With stdcall default, clang's codegen asks for
# the @N form but libcmt ships them cdecl-decorated. Redirect at link time.
OXDK_CRT_HELPERS = \
	/alternatename:__ftol@8=__ftol \
	/alternatename:__ftol2@8=__ftol2

# Default XDK libs (debug). Override OXDK_LIBS in your Makefile before including oxdk.mk.
OXDK_LIBS ?= libcmtd.lib libcpmtd.lib xboxkrnl.lib \
	d3d8d.lib d3dx8d.lib xgraphicsd.lib dsoundd.lib \
	xnetd.lib xonlined.lib xbdm.lib \
	xapilibd.lib xapilib.lib xapilibp.lib

# Build rules
# OBJS handles .cpp, .cc and .c sources. Path separators in the source
# (libSDLx is pulled in with absolute paths) flatten to their basenames
# so a long source tree doesn't create a deeply nested OUTPUT_DIR.
SRCS_BASE := $(notdir $(SRCS))
OBJS_TMP  := $(patsubst %.cpp,%.obj,$(SRCS_BASE))
OBJS_TMP  := $(patsubst %.cc,%.obj,$(OBJS_TMP))
OBJS      := $(addprefix $(OUTPUT_DIR)/,$(patsubst %.c,%.obj,$(OBJS_TMP)))

# Per-source vpath so the .cpp/.c pattern rules can find sources in
# subdirectories (libSDLx is at ../../third-party/libSDLx/...).
VPATH := $(sort $(dir $(SRCS)))

.PHONY: all clean normalize-xdk

all: $(OUTPUT_DIR)/default.xbe
	@echo "=== Build complete: $< ==="

normalize-xdk:
	$(OXDK_DIR)/tools/normalize-xdk.sh $(XDK_DIR)

$(OUTPUT_DIR)/default.xbe: $(OUTPUT_DIR)/$(XBE_TITLE).exe $(CXBE)
	$(CXBE) -MODE:$(XBE_MODE) -TITLE:"$(XBE_NAME)" -LIMIT64MB:$(XBE_LIMIT64MB) -OUT:$@ $<

$(OUTPUT_DIR)/$(XBE_TITLE).exe: $(OBJS)
	lld-link $(OXDK_LDFLAGS) $(OXDK_KERNEL_IMPORTS) $(OXDK_CRT_HELPERS) $(LDFLAGS) \
		/map:$(OUTPUT_DIR)/$(XBE_TITLE).map /out:$@ $^ $(OXDK_LIBS) $(LIBS)

$(OUTPUT_DIR)/%.obj: %.cpp | $(OUTPUT_DIR)
	@mkdir -p $(dir $@)
	clang++ $(OXDK_CXXFLAGS) $(CXXFLAGS) -o $@ $<

$(OUTPUT_DIR)/%.obj: %.cc | $(OUTPUT_DIR)
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
