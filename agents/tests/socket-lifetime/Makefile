include ../../toolchain/toolchain.mk

OUTPUT_DIR ?= ../../.build/socket-lifetime
CXXFLAGS := -std=c++20 -Os -Wall -Wextra -Werror -fno-rtti
CPPFLAGS := -I../../windows -I../../deps/cardio/include
LDFLAGS := -static -static-libgcc -static-libstdc++
SOURCES := runtime.cpp ../../windows/async_io.cpp

.PHONY: amd64 i686
amd64: $(OUTPUT_DIR)/runtime-amd64.exe
i686: $(OUTPUT_DIR)/runtime-i686.exe

$(OUTPUT_DIR)/runtime-amd64.exe: $(SOURCES) ../../windows/async_io.h ../../deps/cardio/include/cardio.h $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(dir $@)
	$(AMD64_CXX) $(CXXFLAGS) $(CPPFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) $(LDFLAGS) -luser32 -lws2_32 -o $@

$(OUTPUT_DIR)/runtime-i686.exe: $(SOURCES) ../../windows/async_io.h ../../deps/cardio/include/cardio.h $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(dir $@)
	$(I686_CXX) $(CXXFLAGS) $(CPPFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) $(LDFLAGS) -luser32 -lws2_32 -o $@
