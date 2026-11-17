include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/temp-directory
CXXFLAGS := -std=c++20 -Os -static -municode -Wall -Wextra -Werror -I../../windows -I../../deps/cardio/include
SOURCES := runtime.cpp ../../windows/win32_temp_directory.cpp ../../windows/win32_files.cpp ../../windows/win32_util.cpp ../../windows/operation_error.cpp ../../windows/binary_codec.cpp

.PHONY: all
all: $(OUTPUT)/runtime-amd64.exe $(OUTPUT)/runtime-i686.exe

$(OUTPUT)/runtime-amd64.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -ladvapi32 -luser32 -o $@

$(OUTPUT)/runtime-i686.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -ladvapi32 -luser32 -o $@
