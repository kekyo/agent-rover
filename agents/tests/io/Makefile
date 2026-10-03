include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/io
CXXFLAGS := -std=c++20 -Os -static -municode -I../../windows -I../../deps/cardio/include
SOURCES := bounded.cpp ../../windows/win32_files.cpp ../../windows/video_recording.cpp ../../windows/binary_codec.cpp ../../windows/operation_error.cpp ../../windows/win32_util.cpp
.PHONY: all
all: $(OUTPUT)/bounded-amd64.exe $(OUTPUT)/bounded-i686.exe
$(OUTPUT)/bounded-amd64.exe: $(SOURCES) $(wildcard ../../windows/*.h) ../../deps/cardio/include/cardio.h $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -luser32 -ladvapi32 -o $@
$(OUTPUT)/bounded-i686.exe: $(SOURCES) $(wildcard ../../windows/*.h) ../../deps/cardio/include/cardio.h $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -luser32 -ladvapi32 -o $@
