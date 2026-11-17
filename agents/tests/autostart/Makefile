include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/autostart
CXXFLAGS := -std=c++20 -Os -static -municode -Wall -Wextra -Werror -I../../windows -I../../.build/generated
SOURCES := runtime.cpp ../../windows/auto_start.cpp ../../windows/agent_gui.cpp ../../windows/agent_log.cpp ../../windows/win32_util.cpp ../../windows/version_banner.cpp

.PHONY: all
all: $(OUTPUT)/runtime-amd64.exe $(OUTPUT)/runtime-i686.exe

$(OUTPUT)/runtime-amd64.exe: $(SOURCES) ../../.build/gui/agent-amd64.res.o $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) ../../.build/gui/agent-amd64.res.o -ladvapi32 -luser32 -lgdi32 -lshell32 -lcomctl32 -o $@

$(OUTPUT)/runtime-i686.exe: $(SOURCES) ../../.build/gui/agent-i686.res.o $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) ../../.build/gui/agent-i686.res.o -ladvapi32 -luser32 -lgdi32 -lshell32 -lcomctl32 -o $@
