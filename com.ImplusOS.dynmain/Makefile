.PHONY: all clean

SERVICE_NAME := $(notdir $(patsubst %/,%,$(CURDIR)))
TOP_BUILD_DIR ?= ../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/Service/$(SERVICE_NAME)
ELF      := $(BUILD_DIR)/dynmain
LDSO_PATH := /Userland/Service/com.ImplusOS.ldso/ld-impl.so

NASM ?= nasm
CC := x86_64-elf-gcc
LD := x86_64-elf-ld

CFLAGS := -Os -ffreestanding -fno-stack-protector -fno-builtin -fno-pic \
          -m64 -mcmodel=large -mno-red-zone \
          -nostdlib -nostartfiles -nodefaultlibs \
          -Wall -Wextra -Wtype-limits -MMD -MP

SO_CFLAGS := -Os -ffreestanding -fno-stack-protector -fno-builtin -fPIC \
             -m64 -mcmodel=large -mno-red-zone -nostdlib -nodefaultlibs \
             -Wall -Wextra -Wtype-limits

all: $(ELF) $(BUILD_DIR)/libdynlib.so $(BUILD_DIR)/libchain.so \
     $(BUILD_DIR)/libdep.so $(BUILD_DIR)/libpreload.so \
     $(BUILD_DIR)/lib/libextra.so

$(BUILD_DIR)/dynmain_c.o: dynmain.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/dynmain.o: dynmain.s
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD_DIR)/libdynlib.so: dynlib.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c dynlib.c -o $(BUILD_DIR)/dynlib.o
	$(LD) -shared -nostdlib --build-id=none -soname libdynlib.so -o $@ $(BUILD_DIR)/dynlib.o

# libdep: gnu2 TLS dialect (TLSDESC) + GNU hash only
$(BUILD_DIR)/libdep.so: libdep.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -mtls-dialect=gnu2 -c libdep.c -o $(BUILD_DIR)/libdep.o
	$(LD) -shared -nostdlib --build-id=none -soname libdep.so \
	      --hash-style=gnu -o $@ $(BUILD_DIR)/libdep.o

# libchain: depends on libdep; linked -z now (eager PLT)
$(BUILD_DIR)/libchain.so: libchain.c $(BUILD_DIR)/libdep.so
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c libchain.c -o $(BUILD_DIR)/libchain.o
	$(LD) -shared -nostdlib --build-id=none -soname libchain.so \
	      -z now -L $(BUILD_DIR) -ldep -o $@ $(BUILD_DIR)/libchain.o

# libpreload: loaded via LD_PRELOAD; lives next to the executable
$(BUILD_DIR)/libpreload.so: libpreload.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c libpreload.c -o $(BUILD_DIR)/libpreload.o
	$(LD) -shared -nostdlib --build-id=none -soname libpreload.so -o $@ $(BUILD_DIR)/libpreload.o

# libextra: only reachable through LD_LIBRARY_PATH (lib/ subdirectory)
$(BUILD_DIR)/lib/libextra.so: libextra.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c libextra.c -o $(BUILD_DIR)/libextra.o
	$(LD) -shared -nostdlib --build-id=none -soname libextra.so -o $@ $(BUILD_DIR)/libextra.o

$(ELF): $(BUILD_DIR)/dynmain.o $(BUILD_DIR)/dynmain_c.o \
        $(BUILD_DIR)/libdynlib.so $(BUILD_DIR)/libchain.so \
        $(BUILD_DIR)/libdep.so $(BUILD_DIR)/lib/libextra.so
	@mkdir -p $(dir $@)
	$(LD) -nostdlib -e _start --build-id=none \
	      --dynamic-linker $(LDSO_PATH) --no-as-needed \
	      --unresolved-symbols=ignore-all \
	      -L $(BUILD_DIR) -L $(BUILD_DIR)/lib -ldynlib -lchain -ldep -lextra \
	      -o $@ $(BUILD_DIR)/dynmain.o $(BUILD_DIR)/dynmain_c.o

clean:
	rm -rf $(BUILD_DIR)

-include $(BUILD_DIR)/dynmain_c.d
