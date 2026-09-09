include ../../toolchain/toolchain.mk
CXXFLAGS := -std=c++20 -Wall -Wextra -Werror -O2 -static -municode
OUTPUT := ../../.build/cleanup

.PHONY: all
all: $(OUTPUT)/amd64/helper.exe $(OUTPUT)/i686/helper.exe $(OUTPUT)/amd64/lifecycle.exe $(OUTPUT)/i686/lifecycle.exe $(OUTPUT)/amd64/repair.exe $(OUTPUT)/i686/repair.exe

$(OUTPUT)/amd64/helper.exe $(OUTPUT)/i686/helper.exe $(OUTPUT)/amd64/lifecycle.exe $(OUTPUT)/i686/lifecycle.exe $(OUTPUT)/amd64/repair.exe $(OUTPUT)/i686/repair.exe: $(TOOLCHAIN_DEPENDENCIES)

REPAIR_SOURCES := repair.cpp ../../windows/win32_util.cpp ../../windows/operation_error.cpp ../../windows/win32_files.cpp ../../windows/binary_codec.cpp
$(OUTPUT)/amd64/repair.exe: $(REPAIR_SOURCES) ../../windows/win32_cleanup.cpp ../../windows/win32_cleanup.h
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -Wno-error=cast-function-type $(REPAIR_SOURCES) -o $@ -ladvapi32 -lbcrypt

$(OUTPUT)/i686/repair.exe: $(REPAIR_SOURCES) ../../windows/win32_cleanup.cpp ../../windows/win32_cleanup.h
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -Wno-error=cast-function-type $(REPAIR_SOURCES) -o $@ -ladvapi32 -lbcrypt

LIFECYCLE_SOURCES := lifecycle.cpp ../../windows/win32_util.cpp ../../windows/command_line.cpp ../../windows/operation_error.cpp ../../windows/win32_files.cpp ../../windows/binary_codec.cpp
LIFECYCLE_HEADERS := $(wildcard ../../windows/*.h) ../../windows/win32_process.cpp

$(OUTPUT)/amd64/lifecycle.exe: $(LIFECYCLE_SOURCES) $(LIFECYCLE_HEADERS)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -Wno-error=type-limits -DPSAPI_VERSION=1 $(LIFECYCLE_SOURCES) -o $@ -lpsapi -lbcrypt

$(OUTPUT)/i686/lifecycle.exe: $(LIFECYCLE_SOURCES) $(LIFECYCLE_HEADERS)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -Wno-error=type-limits -DPSAPI_VERSION=1 $(LIFECYCLE_SOURCES) -o $@ -lpsapi -lbcrypt

$(OUTPUT)/amd64/helper.exe: helper.cpp ../../windows/command_line.cpp
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -Wno-error=type-limits helper.cpp ../../windows/command_line.cpp -o $@ -ladvapi32

$(OUTPUT)/i686/helper.exe: helper.cpp ../../windows/command_line.cpp
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -Wno-error=type-limits helper.cpp ../../windows/command_line.cpp -o $@ -ladvapi32
