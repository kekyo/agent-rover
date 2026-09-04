# agent-rover - A multi-platform TypeScript test driver for GUI applications
# Copyright (c) Kouji Matsui. (@kekyo@mi.kekyo.net)
# Under MIT.
# https://github.com/kekyo/agent-rover

AMD64_CXX ?= x86_64-w64-mingw32-g++
I686_CXX ?= i686-w64-mingw32-g++
TARGET_ROOT ?= dist
AMD64_OUTPUT ?= $(TARGET_ROOT)/agent-amd64.exe
I686_OUTPUT ?= $(TARGET_ROOT)/agent-i686.exe
LEGACY_OUTPUTS := $(TARGET_ROOT)/agent-rover-agent-amd64.exe $(TARGET_ROOT)/agent-rover-agent-i686.exe
SRC_DIR := windows
SOURCES := $(sort $(wildcard $(SRC_DIR)/*.cpp))
GENERATED_DIR ?= .build/generated
OBJ_ROOT ?= .build/obj
AMD64_OBJ_DIR := $(OBJ_ROOT)/amd64
I686_OBJ_DIR := $(OBJ_ROOT)/i686
AMD64_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(AMD64_OBJ_DIR)/%.o,$(SOURCES))
I686_OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(I686_OBJ_DIR)/%.o,$(SOURCES))
DEPENDENCIES := $(AMD64_OBJECTS:.o=.d) $(I686_OBJECTS:.o=.d)
VERSION_TEMPLATE ?= version.h.in
VERSION_HEADER ?= $(GENERATED_DIR)/version.h
SCREW_UP ?= ../node_modules/.bin/screw-up

CXXFLAGS ?= -std=c++17 -Os -s -fno-exceptions -fno-rtti
CPPFLAGS ?= -DPSAPI_VERSION=1
CPPFLAGS += -I$(GENERATED_DIR)
CPPFLAGS_AMD64 ?= -D_WIN32_WINNT=0x0502
CPPFLAGS_I686 ?= -D_WIN32_WINNT=0x0501
LDFLAGS ?= -static -static-libgcc -static-libstdc++ -s -municode
LDLIBS ?= -luser32 -lgdi32 -lpsapi -ladvapi32 -lws2_32 -lbcrypt -lole32 -lmfuuid

.PHONY: all amd64 i686 clean print-output remove-legacy-output version-header

all: remove-legacy-output amd64 i686

amd64: $(AMD64_OUTPUT)

i686: $(I686_OUTPUT)

version-header:
	@mkdir -p $(dir $(VERSION_HEADER))
	$(SCREW_UP) format --input $(VERSION_TEMPLATE) $(VERSION_HEADER) >/dev/null

remove-legacy-output:
	rm -f $(LEGACY_OUTPUTS)

$(AMD64_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | version-header
	@mkdir -p $(dir $@)
	$(AMD64_CXX) $(CXXFLAGS) $(CPPFLAGS) $(CPPFLAGS_AMD64) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(I686_OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | version-header
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS) $(CPPFLAGS) $(CPPFLAGS_I686) -MMD -MP -MF $(@:.o=.d) -c $< -o $@

$(AMD64_OUTPUT): $(AMD64_OBJECTS)
	@mkdir -p $(dir $@)
	$(AMD64_CXX) $(CXXFLAGS) $(CPPFLAGS) $(CPPFLAGS_AMD64) $(AMD64_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(I686_OUTPUT): $(I686_OBJECTS)
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS) $(CPPFLAGS) $(CPPFLAGS_I686) $(I686_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

print-output:
	@printf '%s\n' '$(AMD64_OUTPUT)' '$(I686_OUTPUT)'

clean:
	rm -f $(AMD64_OUTPUT) $(I686_OUTPUT) $(LEGACY_OUTPUTS) $(VERSION_HEADER)
	rm -rf $(OBJ_ROOT)

-include $(DEPENDENCIES)
