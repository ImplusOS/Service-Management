# Userland/Service/Common.mk
#
# Shared build convention for Userland services. A service directory is
# Userland/Service/<name>/ with its own Makefile that `include`s this file
# and only lists its sources -- no service name is hard-coded anywhere.
#
# Each service builds one hot-loadable object:
#   - a position-independent shared object  $(SERVICE_SO)   (default), or
#   - a standalone ELF (the ld.so interpreter) when SERVICE_KIND := elf
# staged at runtime under /Userland/Service/<name>/.

ifeq ($(strip $(ARCH)),)
$(error ARCH must be supplied by the top-level Makefile)
endif

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-elf-
SERVICE_ARCH_CFLAGS ?= -mstrict-align -mno-outline-atomics
else ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
SERVICE_ARCH_CFLAGS ?= -mcmodel=large -mno-red-zone
else
$(error Unsupported ARCH '$(ARCH)'. Use x86_64 or arm64.)
endif

CC   := $(CROSS_COMPILE)gcc
LD   := $(CROSS_COMPILE)ld
AR   := $(CROSS_COMPILE)ar
NASM ?= nasm

# Service identity is derived from the directory name, never spelled out.
SERVICE_NAME    := $(notdir $(patsubst %/,%,$(abspath $(CURDIR)/..)))
REPO_ROOT       := $(abspath $(CURDIR)/../..)
TOP_BUILD_DIR   ?= $(REPO_ROOT)/Build/$(ARCH)
SERVICE_BUILD   := $(TOP_BUILD_DIR)/Userland/Service/$(SERVICE_NAME)
SERVICE_SO      := $(SERVICE_BUILD)/$(SERVICE_NAME).so
SERVICE_RUNTIME_DIR := /Userland/Service/$(SERVICE_NAME)

SERVICE_CFLAGS := -ffreestanding -fno-stack-protector -fno-builtin -fPIC \
                 $(SERVICE_ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
                 -I$(REPO_ROOT)/libc/I_libc/Source/include \
                 -I$(REPO_ROOT)/libc/I_libc/Source/include/sys \
                 -I$(REPO_ROOT)/Userland/Source \
                 -I$(REPO_ROOT)/Userland/API/Source \
                 -Os -g0 -ffunction-sections -fdata-sections -MMD -MP

SERVICE_LDFLAGS := -shared -nostdlib --build-id=none --gc-sections \
                  -soname $(SERVICE_NAME).so

# A service Makefile sets SERVICE_SRCS (paths relative to its own dir) and
# then `$(eval $(SERVICE_SO_RULE))` via the default target below.
$(SERVICE_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SERVICE_CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@
