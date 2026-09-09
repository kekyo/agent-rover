# Compile through the same pinned image in production and native test builds.
TOOLCHAIN_DIRECTORY := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
WINDOWS_TOOLCHAIN := $(TOOLCHAIN_DIRECTORY)../scripts/windows-toolchain.sh
TOOLCHAIN_DEPENDENCIES := $(TOOLCHAIN_DIRECTORY)Containerfile $(TOOLCHAIN_DIRECTORY)toolchain.mk $(WINDOWS_TOOLCHAIN)
AMD64_CXX ?= "$(WINDOWS_TOOLCHAIN)" x86_64-w64-mingw32-g++-win32
I686_CXX ?= "$(WINDOWS_TOOLCHAIN)" i686-w64-mingw32-g++-win32
AMD64_RC ?= "$(WINDOWS_TOOLCHAIN)" x86_64-w64-mingw32-windres
I686_RC ?= "$(WINDOWS_TOOLCHAIN)" i686-w64-mingw32-windres
