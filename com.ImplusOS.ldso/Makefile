.PHONY: all clean

# The userland dynamic linker (PT_INTERP interpreter). It is a standalone
# ELF with a _start entry, not a .so, so it does not use the generic
# SERVICE_SO rule -- but its identity is still the directory name.
SERVICE_NAME  := $(notdir $(patsubst %/,%,$(CURDIR)))
REPO_ROOT     := $(abspath $(CURDIR)/../../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
BUILD_DIR     := $(TOP_BUILD_DIR)/Userland/Service/$(SERVICE_NAME)
ELF           := $(BUILD_DIR)/ld-impl.so

NASM ?= nasm
CC := x86_64-elf-gcc
LD := x86_64-elf-ld

CFLAGS := -Os -ffreestanding -fno-stack-protector -fno-builtin -fno-pic \
          -m64 -mcmodel=large -mno-red-zone \
          -nostdlib -nostartfiles -nodefaultlibs \
          -Wall -Wextra -Wtype-limits -MMD -MP

all: $(ELF)

$(BUILD_DIR)/ldso.o: ldso.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ldso_start.o: ldso_start.s
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(ELF): $(BUILD_DIR)/ldso.o $(BUILD_DIR)/ldso_start.o
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -e _start -Ttext 0x4010000000 --build-id=none -o $@ $^

clean:
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/ldso.d
