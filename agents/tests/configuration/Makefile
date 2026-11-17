include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/configuration
CXXFLAGS := -std=c++20 -Os -static -municode -Wall -Wextra -Werror -I../../windows -I../../deps/cardio/include
SOURCES := loader.cpp ../../windows/agent_configuration.cpp ../../windows/command_line.cpp ../../windows/win32_files.cpp ../../windows/win32_util.cpp ../../windows/operation_error.cpp ../../windows/binary_codec.cpp

.PHONY: all
all: $(OUTPUT)/loader-amd64.exe $(OUTPUT)/loader-i686.exe

EDITOR_SOURCES := editor.cpp ../../windows/config_file.cpp ../../windows/command_line.cpp ../../windows/win32_files.cpp ../../windows/win32_util.cpp ../../windows/operation_error.cpp ../../windows/binary_codec.cpp
SHARE_SOURCES := $(subst loader.cpp,share.cpp,$(SOURCES))

$(OUTPUT)/share-amd64.exe: $(SHARE_SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SHARE_SOURCES) -luserenv -ladvapi32 -luser32 -o $@

$(OUTPUT)/share-i686.exe: $(SHARE_SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SHARE_SOURCES) -luserenv -ladvapi32 -luser32 -o $@

$(OUTPUT)/editor-amd64.exe: $(EDITOR_SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(EDITOR_SOURCES) -ladvapi32 -luser32 -lshell32 -lole32 -o $@

$(OUTPUT)/editor-i686.exe: $(EDITOR_SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(EDITOR_SOURCES) -ladvapi32 -luser32 -lshell32 -lole32 -o $@

$(OUTPUT)/loader-amd64.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -luserenv -ladvapi32 -luser32 -o $@

$(OUTPUT)/loader-i686.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -luserenv -ladvapi32 -luser32 -o $@
