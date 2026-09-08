AMD64_CXX ?= x86_64-w64-mingw32-g++
I686_CXX ?= i686-w64-mingw32-g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Werror -O2 -static -municode
OUTPUT := ../../.build/cleanup

.PHONY: all
all: $(OUTPUT)/amd64/helper.exe $(OUTPUT)/i686/helper.exe

$(OUTPUT)/amd64/helper.exe: helper.cpp
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) $< -o $@ -ladvapi32

$(OUTPUT)/i686/helper.exe: helper.cpp
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) $< -o $@ -ladvapi32
