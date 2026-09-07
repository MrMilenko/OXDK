# sdlx360.mk -- OXDK360 build glue for libSDLx360
#
# Include this from a project Makefile after setting OXDK360_DIR, then append
# $(SDLX360_SRC_LIST) to SRCS and merge SDLX360_DEFINES / SDLX360_INCLUDES into CFLAGS.
# Defines:
#   SDLX360_DIR       -- the vendored libSDLx360 root
#   SDLX360_SRC_LIST  -- the library's .c files, parsed from libSDLx360.vcproj
#   SDLX360_DEFINES   -- what the library and its consumers must be compiled with
#   SDLX360_INCLUDES  -- public and internal headers
#   SDLX360_LIBS      -- the XDK libraries an SDL title needs
#
# Source: https://github.com/frankischilling/libSDLx360 (commit 39d8998), with the fixes
# in oxdk360-ports/patches/libSDLx360.patch applied. SDL 1.2 with D3D9 video, XAudio2
# audio, and the pad through XInput.

SDLX360_ROOT := $(OXDK360_DIR)/third-party/libSDLx360
SDLX360_DIR  := $(SDLX360_ROOT)/SDL

# Straight from the vcproj, so adding or dropping a file upstream is picked up here with
# no second list to keep in step. The paths are Windows-style and relative to the project
# root; strip the leading .\ and flip the separators.
SDLX360_SRC_LIST := $(addprefix $(SDLX360_ROOT)/,$(shell awk -F'"' \
    '/RelativePath="[^"]*\.c"/{print $$2}' $(SDLX360_ROOT)/libSDLx360.vcproj \
    | sed 's|\\|/|g; s|^\./||'))

# From the Release|Xbox 360 configuration of libSDLx360.vcproj:
#   _LIB            standard "this is a static library" hint
#   ENABLE_DIRECTX  selects the D3D9 video and XAudio2 audio backends
#   __powerpc__     what SDL_byteorder.h keys on to choose SDL_BIG_ENDIAN
#   MUST_THREAD_EVENTS  the video backend pumps its own event thread
SDLX360_DEFINES = -D_LIB -DENABLE_DIRECTX -D__powerpc__ -DMUST_THREAD_EVENTS

# The SDL root itself matters: a few internal headers (SDL_error_c.h) live there rather
# than under src/, and the library's own sources include them unqualified.
SDLX360_INCLUDES = -isystem $(SDLX360_DIR)/include \
                   -I$(SDLX360_DIR) \
                   -I$(SDLX360_DIR)/src \
                   -I$(SDLX360_DIR)/src/video \
                   -I$(SDLX360_DIR)/src/audio \
                   -I$(SDLX360_DIR)/src/joystick \
                   -I$(SDLX360_DIR)/src/cdrom \
                   -I$(SDLX360_DIR)/src/events \
                   -I$(SDLX360_DIR)/src/thread \
                   -I$(SDLX360_DIR)/src/thread/xbox \
                   -I$(SDLX360_DIR)/src/timer \
                   -I$(SDLX360_DIR)/src/endian \
                   -I$(SDLX360_DIR)/src/file

# d3d9 and xaudio2 are the backends; xgraphics and d3dx9 are pulled in by the video code
# (XGCopySurface, D3DXCompileShader); xmcore provides the XLFQueue* that xaudio2 needs.
SDLX360_LIBS = d3d9,d3dx9,xgraphics,xaudio2,xmcore,xapilib,oldnames,libcMT

# The library predates this XDK and clang's C99 strictness, but every diagnostic it
# produces has been fixed at the source rather than suppressed -- see the patch. The two
# left are noise from headers we do not own.
SDLX360_WARNINGS = -Wno-pragma-pack -Wno-ignored-attributes -Wno-varargs \
                   -Wno-nonportable-include-path
