include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/configuration
CXXFLAGS := -std=c++20 -Os -static -municode -Wall -Wextra -Werror -I../../windows -I../../deps/cardio/include
SOURCES := loader.cpp ../../windows/agent_configuration.cpp ../../windows/command_line.cpp ../../windows/win32_files.cpp ../../windows/win32_util.cpp ../../windows/operation_error.cpp ../../windows/binary_codec.cpp

.PHONY: all
all: $(OUTPUT)/loader-amd64.exe $(OUTPUT)/loader-i686.exe

$(OUTPUT)/loader-amd64.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -luserenv -ladvapi32 -luser32 -o $@

$(OUTPUT)/loader-i686.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -luserenv -ladvapi32 -luser32 -o $@
