# sdl2x.mk -- OXDK build glue for RXDK-SDL2x
#
# Include this from a sample Makefile after setting OXDK_DIR. Defines:
#   SDL2X_DIR        -- the vendored libSDL2x root
#   SDL2X_SRC_LIST   -- the 128 libSDL2x .c files (parsed from libSDL2x.vcxproj)
#   SDL2X_DEFINES    -- preprocessor flags required to compile libSDL2x + Xbox app code
#   SDL2X_INCLUDES   -- include flags for libSDL2x public + internal headers
#   SDL2X_LIBS       -- the standard XDK link line for an SDL2x app
#
# After include, append $(SDL2X_SRC_LIST) to your SRCS and merge SDL2X_DEFINES /
# SDL2X_INCLUDES into CFLAGS / CXXFLAGS. See ../../examples/sdl2_plasma/Makefile
# for the smallest possible consumer.
#
# Source: https://github.com/Team-Resurgent/RXDK-SDL2x (commit 0525d55)

SDL2X_ROOT  := $(OXDK_DIR)/xbox/third-party/SDL2x
SDL2X_DIR   := $(SDL2X_ROOT)/libSDL2x
SDL2X_OXDK  := $(SDL2X_ROOT)/oxdk

# libSDL2x source list, straight from the vcxproj. One source of truth: if
# upstream adds or drops a .c file, the build follows automatically.
SDL2X_SRC_LIST := $(addprefix $(SDL2X_DIR)/,$(shell awk -F'"' \
    '/<ClCompile Include="/{print $$2}' $(SDL2X_DIR)/libSDL2x.vcxproj \
    | sed 's|\\|/|g'))

# Defines required to compile libSDL2x + Xbox app code:
#
# - XBOX / _XBOX / __XBOX__: the full triple. SDL_platform.h picks
#   SDL_config_xbox_rxdk.h on __XBOX__, and windows.h is included via
#   __XBOX__ vs __WIN32__. Missing any of these and SDL2 thinks it's
#   targeting Windows desktop.
# - _LIB: standard "we're a static library" hint.
# - SDL_DISABLE_IMMINTRIN_H: stops SDL_cpuinfo.h from pulling
#   <immintrin.h> (which transitively pulls SSE3 headers like pmmintrin.h).
#   OXDK targets Pentium III (SSE1 only) so SSE3 types are undefined.
# - -U__SSE__ / -U__MMX__: clang's MSVC-compat mode (-fms-compatibility-
#   version=13.10) emits SSE/MMX intrinsics as forward decls instead of
#   inlining them, so each _mm_loadu_ps/_mm_setzero_si64/etc. becomes an
#   unresolved external at link time. Undefining the feature macros makes
#   SDL2 fall through to its scalar memcpy/blit paths. Modest perf hit.
# - -include interlocked_shim.h: forward-declares the Interlocked*
#   intrinsics so SDL2 source compiles without dragging in clang's
#   <intrin.h>, which would pull the SSE3 headers we just blocked above.
#   Keeps _NTOS_ set (oxdk.mk default) so winbase.h skips its own
#   Interlocked* decls, avoiding the conflict.
SDL2X_DEFINES = -DXBOX -D_XBOX -D__XBOX__ -D_LIB -DSDL_DISABLE_IMMINTRIN_H \
                -U__SSE__ -U__MMX__ \
                -include $(SDL2X_OXDK)/interlocked_shim.h

SDL2X_INCLUDES = -isystem $(SDL2X_DIR)/include -isystem $(SDL2X_DIR)/src

# Standard XDK link line for an SDL2x app. xkbd.lib is the on-screen keyboard
# library that the libSDL2x xbox video driver pulls in.
SDL2X_LIBS = libcmt.lib libcpmt.lib xboxkrnl.lib \
             d3d8.lib d3dx8.lib xgraphics.lib dsound.lib \
             xapilib.lib xkbd.lib
