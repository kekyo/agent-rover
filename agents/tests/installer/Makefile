include ../../toolchain/toolchain.mk
OUTPUT := ../../.build/installer

.PHONY: all
all: $(OUTPUT)/command.exe

$(OUTPUT)/command.exe: command.cpp $(TOOLCHAIN_DEPENDENCIES)
	@mkdir -p $(@D)
	$(I686_CXX) -std=c++20 -Os -static -municode -Wall -Wextra -Werror -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 $< -lole32 -lshell32 -ladvapi32 -luuid -o $@
