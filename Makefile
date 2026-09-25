# Builds the ZKT kernel for i386. See docs/FOUNDING-PROPOSAL.md §7/§10
# and docs/milestones/. Requires the i686-elf cross-compiler; run
# `make toolchain` once if tools/toolchain/i686-elf/ doesn't exist yet.

CROSS_PREFIX := tools/toolchain/i686-elf/bin/i686-elf-
CC := $(CROSS_PREFIX)gcc
LD := $(CROSS_PREFIX)gcc
AR := $(CROSS_PREFIX)ar
STRIP := $(CROSS_PREFIX)strip
OBJCOPY := $(abspath $(CROSS_PREFIX)objcopy)

# The i686-elf compiler defaults to -march=pentiumpro; ZKT targets the
# 80386, so pin code generation, and have the assembler reject post-386
# instructions in .S files and inline asm alike.
ARCHFLAGS := -march=i386 -Wa,-march=i386

INCLUDES := -Izkt/abi -Izkt/arch/i386 -Izkt/drivers -Izkt/fs -Izkt/kernel -Izkt/mm -Izkt/scheduler
# Stack guard pages are 4 KiB; a frame bigger than that could step over
# one without touching it, so flag frames at half that size.
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Wframe-larger-than=2048 \
          -MMD -MP $(ARCHFLAGS) $(INCLUDES)
ASFLAGS := -MMD -MP $(ARCHFLAGS) $(INCLUDES)
LDFLAGS := -ffreestanding -O2 -nostdlib -T zkt/arch/i386/linker.ld

BUILD := build
KERNEL := $(BUILD)/manios-zkt.elf

C_SOURCES := $(wildcard zkt/arch/i386/*.c zkt/drivers/*.c zkt/fs/*.c zkt/kernel/*.c \
                          zkt/mm/*.c zkt/scheduler/*.c)
S_SOURCES := $(wildcard zkt/arch/i386/*.S)
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES)) \
           $(patsubst %.S,$(BUILD)/%.o,$(S_SOURCES))

# Userspace (docs/milestones/M8-userspace.md): libc, then one program
# per .c file -- userland/bin/ goes to /bin, userland/test/ to
# /boot/test -- packed with userland/etc/ into the boot archive the
# kernel links in (zkt/fs/bootfs.c). Like the kernel, programs are
# linked without libgcc. -n drops the page alignment of segments within
# the file: the kernel copies segments rather than mapping file pages,
# so the padding would only waste space in the kernel image.
USER_CFLAGS := -std=gnu11 -ffreestanding -fno-asynchronous-unwind-tables -O2 -g \
               -Wall -Wextra -MMD -MP $(ARCHFLAGS) -Ilibc/include -Izkt/abi
USER_LDFLAGS := -nostdlib -static -T userland/user.ld -Wl,-n

LIBC := $(BUILD)/libc/libc.a
CRT0 := $(BUILD)/libc/crt0.o
LIBC_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard libc/*.c))
USER_PROGRAMS := $(patsubst userland/%.c,%,$(wildcard userland/bin/*.c userland/test/*.c))
USER_OBJECTS := $(patsubst %,$(BUILD)/userland/%.o,$(USER_PROGRAMS))
BOOTFS_FILES := $(patsubst %,$(BUILD)/bootfs/%,$(USER_PROGRAMS)) \
                $(patsubst userland/%,$(BUILD)/bootfs/%,$(wildcard userland/etc/*))
BOOTFS_OBJECT := $(BUILD)/bootfs.o

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

$(BUILD)/libc/string.o: USER_CFLAGS += -fno-tree-loop-distribute-patterns

$(BUILD)/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/libc/%.o: libc/%.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(LIBC): $(LIBC_OBJECTS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/userland/%.o: userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/%.elf: $(BUILD)/userland/%.o $(CRT0) $(LIBC) userland/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ $(CRT0) $< $(LIBC)

$(BUILD)/bootfs/%: $(BUILD)/userland/%.elf
	@mkdir -p $(dir $@)
	$(STRIP) -o $@ $<

$(BUILD)/bootfs/etc/%: userland/etc/%
	@mkdir -p $(dir $@)
	cp $< $@

# Sorted names, fixed owner and timestamps: the same inputs give the
# same archive. objcopy runs inside build/ so the symbols it defines are
# _binary_bootfs_tar_start/_end; the data goes in .rodata.
$(BUILD)/bootfs.tar: $(BOOTFS_FILES)
	tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 \
	    -C $(BUILD)/bootfs -cf $@ $(patsubst $(BUILD)/bootfs/%,%,$(BOOTFS_FILES))

$(BOOTFS_OBJECT): $(BUILD)/bootfs.tar
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf32-i386 -B i386 \
	    --rename-section .data=.rodata,alloc,load,readonly,data,contents bootfs.tar bootfs.o

$(KERNEL): $(OBJECTS) $(BOOTFS_OBJECT) zkt/arch/i386/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -Wl,-Map=$(BUILD)/manios-zkt.map -o $@ $(OBJECTS) $(BOOTFS_OBJECT) -lgcc

run: $(KERNEL)
	tools/qemu-run.sh $(KERNEL)

test: $(KERNEL)
	tests/boot_smoke_test.sh $(KERNEL)
	python3 tests/console_test.py $(KERNEL)

clean:
	rm -rf $(BUILD)

# Keep the unstripped programs for debugging (addr2line, objdump).
.SECONDARY: $(patsubst %,$(BUILD)/userland/%.elf,$(USER_PROGRAMS)) $(USER_OBJECTS) $(CRT0)

-include $(OBJECTS:.o=.d) $(LIBC_OBJECTS:.o=.d) $(CRT0:.o=.d) $(USER_OBJECTS:.o=.d)
