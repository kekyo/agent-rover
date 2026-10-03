include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/log-storage
CXXFLAGS := -std=c++20 -Os -static -municode -I../../windows -I../../vendor/cardio
SOURCES := storage.cpp ../../windows/log_storage.cpp ../../windows/win32_util.cpp
.PHONY: all
all: $(OUTPUT)/storage-amd64.exe $(OUTPUT)/storage-i686.exe
$(OUTPUT)/storage-amd64.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(AMD64_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 $(SOURCES) -luser32 -o $@
$(OUTPUT)/storage-i686.exe: $(SOURCES) $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) $(CXXFLAGS) -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $(SOURCES) -luser32 -o $@
