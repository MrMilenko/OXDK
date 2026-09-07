# oxdk360.mk: build system for cross-compiling Xbox 360 XDK projects
#
# Include this from your project Makefile after setting:
#   OXDK360_DIR - path to this OXDK360 directory
#   XDK_DIR     - path to your extracted Xbox 360 XDK (or set it in the environment)
#   SRCS        - list of source files (.c, .cpp, .S)
#   XEX_TITLE   - output name, used for the PE name inside the XEX
#   XDK_LIBS    - XDK libraries to link, comma separated (default: xapilib,libcMT)
#   XEX_BASE    - image base (default 0x82000000, the title range)
#   XEX_STACK   - stack size (default 0x100000)
#
# Then: make, make deploy, make run.

ifeq ($(OXDK360_DIR),)
$(error OXDK360_DIR must be set to the OXDK360 directory)
endif

# The Xbox 360 needs the patched clang. Stock clang has no xenon-abi feature,
# and without it calls between compiler output and the XDK corrupt each other's
# stacks. scripts/build-llvm.sh builds one.
ifeq ($(OXDK_LLVM),)
$(error No patched LLVM found. Run scripts/build-llvm.sh, or set OXDK_LLVM to \
an existing build directory. See docs/BUILDING-LLVM.md)
endif
CLANG   ?= $(call oxdk-xenon-tool,clang)
CLANGXX ?= $(call oxdk-xenon-tool,clang++)



XDK_DIR ?= $(OXDK_XBOX360_XDK)

ifeq ($(XDK_DIR),)
$(error XDK_DIR must point at an extracted Xbox 360 XDK. See the README for how to \
extract one on a Unix host)
endif

ifeq ($(wildcard $(XDK_DIR)/lib/xbox/xboxkrnl.lib),)
$(error $(XDK_DIR) does not look like an XDK: lib/xbox/xboxkrnl.lib is missing)
endif

XEX_TITLE ?= title
XEX_BASE  ?= 0x82000000
XEX_STACK ?= 0x100000
# The title's id, from its xex.xml if it is a port of an XDK project. Left at zero a title
# still runs, but everything the console keys by title id resolves to nothing: XContent
# save packages, profiles, achievements. Saves then appear to succeed and are not there.
XEX_TITLE_ID ?= 0x0
XDK_LIBS  ?= xapilib,libcMT
OUTPUT_DIR ?= .

TARGET := powerpc-unknown-none-elf

# ---------------------------------------------------------------------------
# Compiler flags
#
# The XDK headers are MSVC headers. They compile under clang in MS compatibility
# mode given the macros they gate on: _WIN32 is what the CRT headers check, _PPC_
# selects the winnt.h architecture block that defines CONTEXT, and _M_PPCBE is what
# the XDK's own headers test. The CRT headers themselves (excpt.h, sys/types.h)
# live in the TechPreview compiler tree, which the XDK expects the compiler to
# supply.
#
# header-shim comes first so xnamath.h can be neutralised; see docs/XEX2.md for
# why, and note that anything genuinely using XNA math needs VMX128 support that
# clang does not have.
# ---------------------------------------------------------------------------

# The XDK ships Microsoft's C++ library, which clang cannot link against: our headers
# are compiled by clang and mangle Itanium, while libcpMT.lib defines the same entities
# with MSVC mangling, so references never meet. Templates and inline code survive that
# because they are emitted into our own objects; iostreams and locale do not, which is
# where a port stops linking (std::locale::_Init, _Locinfo_ctor, _Lockit and friends).
#
# OXDK360_LIBCXX=1 replaces it with libc++ built from our own headers, so every entity
# is ours and there is nothing to mismatch. See oxdk360/libcxx-config/__config_site for
# what is switched off. Default mode is unchanged: C projects need none of this.
ifeq ($(OXDK360_LIBCXX),1)
# common/toolchain.mk finds this. Set it yourself to override.
OXDK360_LIBCXX_DIR ?= $(OXDK_LIBCXX_DIR)
ifeq ($(wildcard $(OXDK360_LIBCXX_DIR)/vector),)
$(error OXDK360_LIBCXX=1 but no libc++ headers at $(OXDK360_LIBCXX_DIR). Set \
OXDK360_LIBCXX_DIR to an LLVM source tree's libcxx/include, or to an \
installed include/c++/v1)
endif
OXDK360_CLANG_RES := $(shell $(CLANG) -print-resource-dir)/include

# char16_t and char32_t are keywords from MSVC 19.00 on and typedefs before it, and
# libc++ declares overloads on both. At compatibility 14 they collapse onto unsigned
# short and the overloads redefine each other.
OXDK360_MSC_COMPAT := 19
OXDK360_MSC_DEF    := -D_MSC_VER=1900 -D_MSC_FULL_VER=190023026

# Our __config_site wins for <__config_site>, then libc++ proper, then the C shims that
# clean up what the XDK CRT spells differently, then clang's own builtin headers.
OXDK360_LIBCXX_INC := -nostdinc++ \
                      -isystem $(OXDK360_DIR)/oxdk360/libcxx-config \
                      -isystem $(OXDK_ROOT)/common/libcxx \
                      -isystem $(OXDK360_LIBCXX_DIR) \
                      -isystem $(OXDK360_DIR)/oxdk360/libcxx-cshim \
                      -isystem $(OXDK360_CLANG_RES)
# C files take the shims too, for what the XDK CRT lacks outright.
OXDK360_LIBCXX_C_INC := -isystem $(OXDK360_DIR)/oxdk360/libcxx-cshim
# clang emits the v3 EH personality, which the XDK CRT does not ship, and enabling
# exceptions pulls in std::exception and friends that want a real libc++abi.
# NOMINMAX stops the XDK headers defining min/max as macros over std::min/std::max.
#
# The _DEFINED macros settle a spelling argument. For this target clang's own headers
# make size_t and friends unsigned long where the XDK makes them unsigned int; both are
# 32 bits, but they are different types to C++ and each header redefines the other's
# typedef. libc++ pulls clang's <stddef.h> in regardless, so clang's spelling is the one
# that has to win, and these tell the XDK headers to leave theirs alone. C files never
# see libc++ and so keep the XDK's own spellings, which is why this is on the C++ path.
# stdint.h is force included for the other half of that bargain: having told the XDK not
# to define intptr_t and uintptr_t, something must, and an XDK header that wants one is
# not obliged to have included <cstdint> first.
OXDK360_LIBCXX_CXX := -DNOMINMAX -include stdint.h \
                      -D_PTRDIFF_T_DEFINED -D_INTPTR_T_DEFINED -D_UINTPTR_T_DEFINED
# -isystem so libc++ wins any header the XDK also spells.
OXDK360_SHIM_INC := -isystem $(OXDK360_DIR)/oxdk360/header-shim
OXDK360_INC := -isystem $(XDK_DIR)/include/xbox \
               -isystem $(XDK_DIR)/TechPreview/Jul12Compiler/include/xbox
else
OXDK360_MSC_COMPAT := 14
OXDK360_MSC_DEF    := -D_MSC_VER=1400 -D_MSC_FULL_VER=140050727
OXDK360_SHIM_INC := -I $(OXDK360_DIR)/oxdk360/header-shim
OXDK360_INC := -I $(XDK_DIR)/include/xbox \
               -I $(XDK_DIR)/TechPreview/Jul12Compiler/include/xbox
endif

# D3DX and XNA math. clang has no VMX128, so the only route is the XDK's own scalar
# fallback, and that fallback is written in C++. d3dx9math.inl uses reinterpret_cast
# inside exactly that branch. A C file therefore cannot reach the real headers at all
# and gets the stand-ins in header-shim; a C++ file can, and gets the real declarations,
# which is what the XDK's own xuirender.h and xui.h need to compile.
OXDK360_CXX_XNA := -D_XM_NO_INTRINSICS_

# There is no unwinder for this target and no libc++abi, so C++ exceptions cannot work in
# any mode. Without this clang still emits landing pads and calls to _Unwind_Resume, which
# then fail to link, or worse link against something that cannot unwind. Titles that
# want try/catch need an exception runtime first; see docs.
OXDK360_CXX_NOEH := -fno-exceptions

# _MSC_FULL_VER decides which va_start the XDK's vadefs.h uses, and clang defines
# neither it nor _MSC_VER for this target. Left undefined the header takes its old
# path, deriving the va_list from the address of the last named parameter:
#
#     ap = (va_list)(&v + 1)
#
# which only works because Microsoft's compiler keeps named parameters in their home
# slots in the caller's parameter save area. clang keeps them in registers and spills
# to its own frame, so ap points at the callee's locals and every variable argument is
# garbage. Set high enough, the header uses __va_start instead, which clang implements
# correctly. This is why printf("%s") silently produced empty strings.
OXDK360_DEF := -D_WIN32 -D_M_PPCBE -D_M_PPC -D_PPC_ -D_XBOX -D_XENON -DXBOX \
               $(OXDK360_MSC_DEF) \
               -D_SIZE_T_DEFINED -D_W64=

# -fshort-wchar because the XDK is MSVC, where wchar_t is 16 bits. Without it every
# L"..." literal in an XDK header is the wrong width and wide string calls are wrong.
# -fno-autolink because the CRT headers carry #pragma comment(lib, ...), which clang
# would otherwise turn into a dependent library specifier naming a library the way
# Windows spells it.
OXDK360_BASEFLAGS := -target $(TARGET) -c -ffreestanding -fno-builtin \
                     -fms-extensions -fms-compatibility \
                     -fms-compatibility-version=$(OXDK360_MSC_COMPAT) \
                     -fshort-wchar -fno-autolink -fdelayed-template-parsing \
                     -Wno-invalid-token-paste \
                     $(OXDK360_DEF)

# The Xenon calling convention lives behind a subtarget feature in a patched clang.
# Without it clang emits SysV PowerPC frames, and every call into XDK or kernel code
# needs a hand written shim to give the callee the parameter save area it expects.
# See tools/abitest/abicheck.py, which reports whether a given clang has it.
ifeq ($(XENON_ABI),1)
# A stock clang does not have the feature and says so with a WARNING, then compiles the
# whole title with SysV frames anyway. Nothing downstream notices: it links, it wraps, it
# passes xexcheck, and it is wrong in every function that calls into XDK code. Ask the
# compiler up front and refuse to build rather than ship that.
OXDK360_NO_XENON := $(shell echo 'int f(void){return 0;}' | \
    $(CLANG) -target $(TARGET) -c -x c - -o /dev/null \
    -Xclang -target-feature -Xclang +xenon-abi 2>&1 | grep -c 'not a recognized feature')
ifneq ($(OXDK360_NO_XENON),0)
$(error XENON_ABI=1 but $(CLANG) does not have the xenon-abi feature. Build the patched \
LLVM in llvm/ and point CLANG at it, or unset XENON_ABI. See tools/abitest/abicheck.py)
endif
OXDK360_BASEFLAGS += -Xclang -target-feature -Xclang +xenon-abi
endif

# Include order decides which <vector> a C++ file gets: the first -isystem holding the
# name wins, and the XDK ships its own copy of the whole C++ library. libc++ goes ahead
# of the XDK directories, not after them, or every header resolves back to Microsoft's.
# Both are derived here rather than above so they pick up the ABI feature appended just
# now, and the C path deliberately keeps the XDK's own C headers first.
OXDK360_CFLAGS   := $(OXDK360_BASEFLAGS) $(OXDK360_LIBCXX_C_INC) $(OXDK360_SHIM_INC) \
                    $(OXDK360_INC)
OXDK360_CXXFLAGS := $(OXDK360_BASEFLAGS) $(OXDK360_LIBCXX_INC) $(OXDK360_INC) \
                    $(OXDK360_LIBCXX_CXX) $(OXDK360_CXX_XNA) $(OXDK360_CXX_NOEH)

OXDK360_ASFLAGS := -target $(TARGET) -c

CFLAGS   ?= -O2 -Wall
CXXFLAGS ?= -O2 -Wall
ASFLAGS  ?=

CSRCS   := $(filter %.c,$(SRCS))
CXXSRCS := $(filter %.cpp %.cc,$(SRCS))
SSRCS   := $(filter %.S,$(SRCS))
OBJS    := $(CSRCS:.c=.o) $(CXXSRCS:.cpp=.o) $(SSRCS:.S=.o)
OBJS    := $(OBJS:.cc=.o)

# The 64-bit arithmetic helpers clang calls into; see oxdk360/builtins/builtins.c for
# why the XDK supplies none of them. Built per title so it picks up the title's flags.
OXDK360_BUILTINS   := $(OXDK360_DIR)/oxdk360/builtins/builtins.c
OXDK360_BUILTINS_O := $(OUTPUT_DIR)/oxdk360_builtins.o
OBJS += $(OXDK360_BUILTINS_O)

# libc++ keeps a few entities out of its headers and in its prebuilt library, which
# this target has none of. Built per title for the same reason as the builtins.
ifeq ($(OXDK360_LIBCXX),1)
OXDK360_LIBCXX_SRC := $(OXDK360_DIR)/oxdk360/libcxx-shim/libcxx_runtime.cpp
OXDK360_LIBCXX_O   := $(OUTPUT_DIR)/oxdk360_libcxx.o
OBJS += $(OXDK360_LIBCXX_O)

# Iostreams, locale and streambuf are the part of libc++ that is compiled rather than
# inline, and they need libc++'s own sources built for this target. That needs an LLVM
# checkout, so it is a second opt-in: a title that only uses containers, strings and
# algorithms needs none of it. Set OXDK360_LIBCXX_LIB=1 to link it.
ifeq ($(OXDK360_LIBCXX_LIB),1)
ifeq ($(LIBCXX_SRC),)
LIBCXX_SRC := $(firstword $(wildcard $(HOME)/llvm-project/libcxx $(HOME)/llvm-xenon/libcxx \
                                     $(LLVM_PREFIX)/../libcxx))
endif
ifeq ($(LIBCXX_SRC),)
$(error OXDK360_LIBCXX_LIB=1 needs libc++'s sources: set LIBCXX_SRC to the libcxx \
directory of an LLVM checkout matching your libc++ headers)
endif
OXDK360_LIBCXX_A := $(OXDK360_DIR)/oxdk360/libcxx-lib/libcxx-xenon.a
OBJS += $(OXDK360_LIBCXX_A)
endif
endif

LINKER_SCRIPT ?= $(OXDK360_DIR)/oxdk360/title.ld
XEX := $(OUTPUT_DIR)/default.xex

all: $(XEX)

# Deliberately after `all`: the first rule in a makefile is the default goal, and
# putting this above it makes `make` build the helper object and nothing else.
# Deliberately without the project's CFLAGS. These are the toolchain's own translation
# units, and a project is entitled to flags that would break them. A force-included
# header that redefines fopen, say, which is exactly how a port makes the console's
# backslash paths work everywhere.
$(OXDK360_BUILTINS_O): $(OXDK360_BUILTINS)
	@mkdir -p $(dir $@)
	$(CLANG) $(OXDK360_CFLAGS) -O2 $< -o $@
	@$(OBJCOPY) --remove-section=.deplibs $@ 2>/dev/null || true

$(OXDK360_LIBCXX_O): $(OXDK360_LIBCXX_SRC)
	@mkdir -p $(dir $@)
	$(CLANG) $(OXDK360_CXXFLAGS) -O2 -std=c++20 $< -o $@
	@$(OBJCOPY) --remove-section=.deplibs $@ 2>/dev/null || true

# Built once and shared by every title, since it depends only on the toolchain flags.
$(OXDK360_LIBCXX_A):
	$(MAKE) -C $(OXDK360_DIR)/oxdk360/libcxx-lib \
	    CLANG='$(CLANG)' LLVM_AR='$(LLVM_AR)' LIBCXX_SRC='$(LIBCXX_SRC)' \
	    OXDK360_CXXFLAGS='$(OXDK360_CXXFLAGS)'

# -fno-autolink covers the pragma, but MS compatibility mode still records the request
# in a .deplibs section, and ld.lld tries to honour it by looking for a library named
# the Windows way. Nothing here wants that: libraries come from XDK_LIBS.
#
# Not silenced: if this does not run, the failure surfaces much later as ld.lld
# asking for libc++.lib, which says nothing about the missing tool.
OXDK360_STRIP_DEPLIBS = $(OBJCOPY) --remove-section=.deplibs $@

%.o: %.c
	$(CLANG) $(OXDK360_CFLAGS) $(CFLAGS) $< -o $@
	@$(OXDK360_STRIP_DEPLIBS)

%.o: %.cpp
	$(CLANG) $(OXDK360_CXXFLAGS) $(CXXFLAGS) $< -o $@
	@$(OXDK360_STRIP_DEPLIBS)

%.o: %.cc
	$(CLANG) $(OXDK360_CXXFLAGS) $(CXXFLAGS) $< -o $@
	@$(OXDK360_STRIP_DEPLIBS)

%.o: %.S
	$(CLANG) $(OXDK360_ASFLAGS) $(ASFLAGS) $< -o $@

$(XEX): $(OBJS) $(OXDK360_DIR)/tools/cxex/cxex
	python3 $(OXDK360_DIR)/tools/oxdklink/oxdklink.py $(OBJS) \
	    -T $(LINKER_SCRIPT) -o $@ \
	    --xdk "$(XDK_DIR)" --libs $(XDK_LIBS) \
	    --base $(XEX_BASE) --stack $(XEX_STACK) --name $(XEX_TITLE).exe \
	    --title-id $(XEX_TITLE_ID)

$(OXDK360_DIR)/tools/cxex/cxex:
	$(MAKE) -C $(OXDK360_DIR)/tools/cxex

# ---------------------------------------------------------------------------
# Console
#
# XBDM_HOST is the console's address. Everything here is optional: a build does
# not need a console.
# ---------------------------------------------------------------------------

XBDM := python3 $(OXDK360_DIR)/tools/xbdm/xbdm.py
XEX_REMOTE_DIR ?= Hdd1:\OXDK360

check-host:
	@test -n "$(XBDM_HOST)" || \
	    { echo "set XBDM_HOST to your console's address, e.g. XBDM_HOST=192.168.1.50 make run"; exit 1; }

deploy: $(XEX) check-host
	$(XBDM) $(XBDM_HOST) cmd 'mkdir name="$(XEX_REMOTE_DIR)"' >/dev/null 2>&1 || true
	$(XBDM) $(XBDM_HOST) deploy $(XEX) '$(XEX_REMOTE_DIR)\default.xex'

run: deploy
	$(XBDM) $(XBDM_HOST) cmd 'magicboot title="$(XEX_REMOTE_DIR)\default.xex" directory="$(XEX_REMOTE_DIR)"'

watch: check-host
	$(XBDM) $(XBDM_HOST) listen

reboot: check-host
	$(XBDM) $(XBDM_HOST) cmd 'magicboot cold'

clean:
	rm -f $(OBJS) $(XEX)
	rm -rf $(OUTPUT_DIR)/.oxdklink

.PHONY: all clean deploy run watch reboot check-host
