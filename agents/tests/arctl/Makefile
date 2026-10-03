include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/arctl
CXXFLAGS := -std=c++20 -Os -static -municode

.PHONY: all
all: $(OUTPUT)/launch-amd64.exe

$(OUTPUT)/launch-amd64.exe: launch.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) $< -o $@
