include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/gui
CXXFLAGS := -std=c++20 -Os -static -municode -I../../windows -I../../.build/generated
SOURCES := viewer.cpp ../../windows/agent_gui.cpp ../../windows/agent_log.cpp ../../windows/win32_util.cpp ../../windows/version_banner.cpp

.PHONY: all
all: $(OUTPUT)/viewer-amd64.exe $(OUTPUT)/viewer-i686.exe $(OUTPUT)/control-amd64.exe

$(OUTPUT)/viewer-amd64.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -luser32 -lgdi32 -lshell32 -lcomctl32 -o $@

$(OUTPUT)/viewer-i686.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -luser32 -lgdi32 -lshell32 -lcomctl32 -o $@

$(OUTPUT)/control-amd64.exe: control.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $< -luser32 -o $@
