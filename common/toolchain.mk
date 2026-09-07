# Finding the compiler and its tools.
#
# Shared by both consoles. Nothing is required to be in a fixed place and
# nothing assumes a package manager. Anything set in the environment is used as
# given; anything not set is searched for.
#
# The two consoles do not need the same compiler. The Xbox 360 needs a clang
# with the Xenon ABI patches, which stock clang does not have. The original
# Xbox needs a clang that can target x86. One LLVM built with both PowerPC and
# X86 covers both, and scripts/build-llvm.sh builds it that way.

# Where OXDK is, worked out from this file, so anything can include it without
# being told.
ifeq ($(OXDK_ROOT),)
OXDK_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif

# A patched LLVM build, if there is one. Only the Xbox 360 requires it.
ifeq ($(OXDK_LLVM),)
OXDK_LLVM := $(firstword $(wildcard \
    $(HOME)/oxdk-llvm/build \
    $(HOME)/llvm-xenon/build \
    /opt/oxdk-llvm))
endif

# Where any LLVM might be. Used for tools that are not target specific, and for
# the original Xbox, which any clang with an x86 backend can build.
OXDK_LLVM_DIRS := \
    $(LLVM_PREFIX) \
    /opt/homebrew/opt/llvm \
    /usr/local/opt/llvm \
    /usr/lib/llvm-23 \
    /usr/lib/llvm-22 \
    /usr/lib/llvm-21 \
    /usr/lib/llvm-20 \
    /usr/local/llvm \
    /opt/homebrew \
    /usr/local \
    /usr

# First directory that has the named tool, else the bare name so PATH is tried.
# Tools are looked up one at a time, because a build that has clang may not
# have lld and there is no reason to reject it for that.
oxdk-tool = $(firstword $(wildcard $(addsuffix /bin/$(1),$(OXDK_LLVM_DIRS))) $(1))

# The same search, but looking in the patched build first.
oxdk-xenon-tool = $(firstword \
    $(wildcard $(OXDK_LLVM)/bin/$(1)) \
    $(wildcard $(addsuffix /bin/$(1),$(OXDK_LLVM_DIRS))) $(1))

LLD_LINK ?= $(call oxdk-tool,lld-link)
LLD      ?= $(call oxdk-tool,ld.lld)
OBJCOPY  ?= $(call oxdk-tool,llvm-objcopy)
LLVM_AR  ?= $(call oxdk-tool,llvm-ar)
LLVM_NM  ?= $(call oxdk-tool,llvm-nm)

# libc++ headers, used header only. An LLVM source tree is preferred, because
# whoever built a compiler already has one and it needs no install step. Each
# console supplies the headers CMake would otherwise have generated.
ifeq ($(OXDK_LIBCXX_DIR),)
OXDK_LIBCXX_DIR := $(abspath $(firstword $(wildcard \
    $(OXDK_LLVM)/../libcxx/include \
    $(HOME)/oxdk-llvm/libcxx/include \
    $(HOME)/llvm-xenon/libcxx/include \
    $(addsuffix /include/c++/v1,$(OXDK_LLVM_DIRS)))))
endif

export OXDK_LLVM OXDK_LIBCXX_DIR LLD_LINK LLD OBJCOPY LLVM_AR LLVM_NM

# The XDKs. You supply these; OXDK does not ship them. Set XDK_DIR to override,
# otherwise the usual places are searched.
#
# The two consoles have different XDKs and different layouts, so they are found
# separately and the console's own makefile picks the one it wants.
OXDK_XBOX_XDK := $(firstword $(wildcard \
    $(OXDK_ROOT)/xbox/xdk/lib/xboxkrnl.lib))
OXDK_XBOX_XDK := $(patsubst %/lib/xboxkrnl.lib,%,$(OXDK_XBOX_XDK))

OXDK_XBOX360_XDK := $(firstword $(wildcard \
    $(HOME)/xdk360/XDK/lib/xbox/xboxkrnl.lib \
    $(HOME)/xdk360-extract/sdk/XDK/lib/xbox/xboxkrnl.lib \
    /opt/xdk360/XDK/lib/xbox/xboxkrnl.lib))
OXDK_XBOX360_XDK := $(patsubst %/lib/xbox/xboxkrnl.lib,%,$(OXDK_XBOX360_XDK))

export OXDK_XBOX_XDK OXDK_XBOX360_XDK
