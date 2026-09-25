# Builds the M1 ZKT kernel for i386. See docs/FOUNDING-PROPOSAL.md §7
# and §10. Requires the i686-elf cross-compiler; run `make toolchain`
# once if tools/toolchain/i686-elf/ doesn't exist yet.

CROSS_PREFIX := tools/toolchain/i686-elf/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
LD := $(CROSS_PREFIX)gcc

CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra \
          -Izkt/arch/i386 -Izkt/drivers -Izkt/kernel
ASFLAGS := -Izkt/arch/i386
LDFLAGS := -ffreestanding -O2 -nostdlib -lgcc -T zkt/arch/i386/linker.ld

BUILD := build
KERNEL := $(BUILD)/manios-zkt.elf

C_SOURCES := $(wildcard zkt/arch/i386/*.c zkt/drivers/*.c zkt/kernel/*.c)
S_SOURCES := $(wildcard zkt/arch/i386/*.S)
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SOURCES))

.PHONY: all toolchain run test clean

all: $(KERNEL)

toolchain:
	tools/toolchain/build-i686-elf-toolchain.sh

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) zkt/arch/i386/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

run: $(KERNEL)
	tools/qemu-run.sh $(KERNEL)

test: $(KERNEL)
	tests/boot_smoke_test.sh $(KERNEL)

clean:
	rm -rf $(BUILD)
