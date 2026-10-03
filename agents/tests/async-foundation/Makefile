# Compatibility gate for plan1-fix-hangup-agent.md, stage 1.
# Use an unmodified cardio include directory inside the checkout or
# AGENT_ROVER_BUILD_DIR so the pinned toolchain container can read it.

include ../../toolchain/toolchain.mk

OUTPUT_DIR ?= ../../.build/async-foundation
CARDIO_INCLUDE ?= ../../deps/cardio/include
CXXFLAGS ?= -std=c++20 -Os -fno-exceptions -fno-rtti
CPPFLAGS ?=
LDFLAGS ?= -static -static-libgcc -static-libstdc++

.PHONY: all core handles runtime standard-threads

all: core handles runtime

core: $(OUTPUT_DIR)/core-amd64.exe $(OUTPUT_DIR)/core-i686.exe

handles: $(OUTPUT_DIR)/handles-amd64.exe $(OUTPUT_DIR)/handles-i686.exe

# Cancellable I/O reports failures through promises and therefore uses exceptions.
runtime: $(OUTPUT_DIR)/runtime-amd64.exe $(OUTPUT_DIR)/runtime-i686.exe

$(OUTPUT_DIR)/runtime-amd64.exe $(OUTPUT_DIR)/runtime-i686.exe: CXXFLAGS := $(filter-out -fno-exceptions,$(CXXFLAGS))
$(OUTPUT_DIR)/runtime-amd64.exe $(OUTPUT_DIR)/runtime-i686.exe: runtime.cpp $(CARDIO_INCLUDE)/cardio.h $(TOOLCHAIN_DEPENDENCIES)

# This independent probe distinguishes missing C++ threading support from a
# cardio-specific error. It is diagnostic, not an expected-failure test.
standard-threads: $(OUTPUT_DIR)/standard-threads-amd64.exe $(OUTPUT_DIR)/standard-threads-i686.exe

$(OUTPUT_DIR)/core-amd64.exe: core.cpp $(CARDIO_INCLUDE)/cardio.h $(TOOLCHAIN_DEPENDENCIES)

$(OUTPUT_DIR)/core-i686.exe: core.cpp $(CARDIO_INCLUDE)/cardio.h $(TOOLCHAIN_DEPENDENCIES)

$(OUTPUT_DIR)/handles-amd64.exe: handles.cpp $(CARDIO_INCLUDE)/cardio.h $(TOOLCHAIN_DEPENDENCIES)

$(OUTPUT_DIR)/handles-i686.exe: handles.cpp $(CARDIO_INCLUDE)/cardio.h $(TOOLCHAIN_DEPENDENCIES)

$(OUTPUT_DIR)/%-amd64.exe: %.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(dir $@)
	$(AMD64_CXX) $(CXXFLAGS) $(CPPFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -I"$(CARDIO_INCLUDE)" $< $(LDFLAGS) -luser32 -lws2_32 -o $@

$(OUTPUT_DIR)/%-i686.exe: %.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS) $(CPPFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -I"$(CARDIO_INCLUDE)" $< $(LDFLAGS) -luser32 -lws2_32 -o $@
