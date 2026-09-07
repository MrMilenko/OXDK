# libc++'s library half, built for the console.
#
# Most of libc++ lives in headers, but iostreams, locale and the streambuf machinery are
# compiled into libc++.a on a normal platform, and there is no such library for this
# target. This builds one from libc++'s own sources using the title's own flags, so what
# a title links is exactly what its headers declared.
#
# Driven by oxdk360.mk when OXDK360_LIBCXX=1. Needs an LLVM checkout whose libc++ is the
# same release as the headers being compiled against; mixing releases mismatches the
# inline half against the compiled half, which links and then misbehaves.
#
#   make LIBCXX_SRC=~/llvm-project/libcxx CLANG=... OXDK360_CXXFLAGS='...'

ifeq ($(LIBCXX_SRC),)
$(error LIBCXX_SRC must point at the libcxx directory of an LLVM source checkout)
endif
ifeq ($(wildcard $(LIBCXX_SRC)/src/locale.cpp),)
$(error $(LIBCXX_SRC) does not look like libc++: src/locale.cpp is missing)
endif

LLVM_AR ?= llvm-ar
BUILD   ?= build
LIB     ?= libcxx-xenon.a

# What a title uses. The omissions are deliberate, not a subset that happened to build:
#
#   atomic, barrier, condition_variable*, future, mutex*, shared_mutex, thread
#                  the config has threads off, and libc++'s Win32 thread backend is
#                     written against fiber-local storage the console has no header for
#   chrono         wants a clock backend; titles read time through the XDK
#   charconv, ryu  need libc++'s internal shared/fp_bits.h, which is not shipped
#   print, random  stdout formatting and a random device the console does not have
#   string         pulls in charconv; oxdk360/libcxx-shim instantiates what is needed
#   expected       only bad_expected_access, which needs exceptions
SOURCES := \
	algorithm any bind call_once error_category exception fstream functional hash \
	ios ios.instantiations iostream locale memory memory_resource new new_handler \
	new_helpers optional ostream random_shuffle regex stdexcept strstream \
	system_error typeinfo valarray variant vector verbose_abort \
	support/win32/locale_win32 support/win32/support

OBJS := $(foreach s,$(SOURCES),$(BUILD)/$(subst /,--,$(s)).o)

all: $(LIB)

$(LIB): $(OBJS)
	@rm -f $@
	$(LLVM_AR) rcs $@ $(OBJS)
	@echo "  libc++ for Xenon: $(words $(OBJS)) objects -> $@"

# _LIBCPP_BUILDING_LIBRARY exposes the definitions the headers declare as external.
# _LIBCPP_CRT_FUNC tags Microsoft runtime declarations for import from a DLL; empty
# here, because the things it tags are supplied by oxdk360/libcxx-shim instead.
define OXDK360_LIBCXX_RULE
$(BUILD)/$(subst /,--,$(1)).o: $(LIBCXX_SRC)/src/$(1).cpp
	@mkdir -p $$(dir $$@)
	$$(CLANG) $$(OXDK360_CXXFLAGS) -std=c++20 -w -O2 \
	    -D_LIBCPP_BUILDING_LIBRARY -D_LIBCPP_CRT_FUNC= -I$$(LIBCXX_SRC)/src \
	    -c $$< -o $$@
endef
$(foreach s,$(SOURCES),$(eval $(call OXDK360_LIBCXX_RULE,$(s))))

clean:
	rm -rf $(BUILD) $(LIB)

.PHONY: all clean
