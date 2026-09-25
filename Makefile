# Builds the ZKT kernel for i386. See docs/FOUNDING-PROPOSAL.md §7/§10
# and docs/milestones/. Requires the i686-elf cross-compiler; run
# `make toolchain` once if tools/toolchain/i686-elf/ doesn't exist yet.

CROSS_PREFIX := tools/toolchain/i686-elf/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
LD := $(CROSS_PREFIX)gcc

# The i686-elf compiler defaults to -march=pentiumpro; ZKT targets the
# 80386, so pin code generation, and have the assembler reject post-386
# instructions in .S files and inline asm alike.
ARCHFLAGS := -march=i386 -Wa,-march=i386

INCLUDES := -Izkt/arch/i386 -Izkt/drivers -Izkt/kernel -Izkt/mm -Izkt/scheduler
# Stack guard pages are 4 KiB; a frame bigger than that could step over
# one without touching it, so flag frames at half that size.
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Wframe-larger-than=2048 \
          -MMD -MP $(ARCHFLAGS) $(INCLUDES)
ASFLAGS := -MMD -MP $(ARCHFLAGS) $(INCLUDES)
LDFLAGS := -ffreestanding -O2 -nostdlib -T zkt/arch/i386/linker.ld

BUILD := build
KERNEL := $(BUILD)/manios-zkt.elf

C_SOURCES := $(wildcard zkt/arch/i386/*.c zkt/drivers/*.c zkt/kernel/*.c zkt/mm/*.c \
                          zkt/scheduler/*.c)
S_SOURCES := $(wildcard zkt/arch/i386/*.S)
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SOURCES))

.PHONY: all toolchain run test clean

all: $(KERNEL)

toolchain:
	tools/toolchain/build-i686-elf-toolchain.sh

# Stops GCC from turning memset/memcpy's own loops into calls to themselves.
$(BUILD)/zkt/kernel/kstring.o: CFLAGS += -fno-tree-loop-distribute-patterns

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS) zkt/arch/i386/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -Wl,-Map=$(BUILD)/manios-zkt.map -o $@ $(OBJECTS) -lgcc

run: $(KERNEL)
	tools/qemu-run.sh $(KERNEL)

test: $(KERNEL)
	tests/boot_smoke_test.sh $(KERNEL)
	python3 tests/console_test.py $(KERNEL)

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
