include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/gui
CXXFLAGS := -std=c++20 -Os -static -municode -I../../windows -I../../.build/generated
SOURCES := viewer.cpp ../../windows/agent_gui.cpp ../../windows/auto_start.cpp ../../windows/agent_log.cpp ../../windows/win32_util.cpp ../../windows/version_banner.cpp
RESOURCES := ../../windows/agent.rc ../../windows/agent.manifest ../../windows/resource.h ../../../images/agent-rover.ico ../../.build/generated/version.h ../../../agent-rover/src/update/legacy-releases.json
RESOURCE_INCLUDES := -I../../windows -I../../.build/generated -I../../../agent-rover/src/update -I../../../images

.PHONY: all
all: $(OUTPUT)/viewer-amd64.exe $(OUTPUT)/viewer-i686.exe $(OUTPUT)/control-amd64.exe

$(OUTPUT)/viewer-amd64.exe: $(SOURCES) $(OUTPUT)/agent-amd64.res.o $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) $(OUTPUT)/agent-amd64.res.o -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32 -o $@

$(OUTPUT)/viewer-i686.exe: $(SOURCES) $(OUTPUT)/agent-i686.res.o $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) $(OUTPUT)/agent-i686.res.o -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32 -o $@

$(OUTPUT)/agent-amd64.res.o: $(RESOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_RC) $(RESOURCE_INCLUDES) $< $@

$(OUTPUT)/agent-i686.res.o: $(RESOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_RC) $(RESOURCE_INCLUDES) $< $@

$(OUTPUT)/control-amd64.exe: control.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $< -luser32 -o $@

$(OUTPUT)/control-i686.exe: control.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $< -luser32 -o $@
