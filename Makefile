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
# -mno-80387: a 386 may have no floating-point unit, so nothing may use
# one (floating-point code then fails to link, for want of libgcc).
ARCHFLAGS := -march=i386 -Wa,-march=i386 -mno-80387

INCLUDES := -Izkt/abi -Izkt/arch/i386 -Izkt/drivers -Izkt/fs -Izkt/kernel -Izkt/mm -Izkt/net \
            -Izkt/scheduler
# Stack guard pages are 4 KiB; a frame bigger than that could step over
# one without touching it, so flag frames at half that size.
CFLAGS := -std=gnu11 -ffreestanding -O2 -g -Wall -Wextra -Wframe-larger-than=2048 \
          -MMD -MP $(ARCHFLAGS) $(INCLUDES)
ASFLAGS := -MMD -MP $(ARCHFLAGS) $(INCLUDES)
LDFLAGS := -ffreestanding -O2 -nostdlib -T zkt/arch/i386/linker.ld

BUILD := build
KERNEL := $(BUILD)/manios-zkt.elf
VERSION := $(shell cat VERSION)
CFLAGS += -DMANIOS_VERSION='"$(VERSION)"'

# The ManiOS boot loader (M14, ADR-0006): boot sectors for disks (mbr)
# and CDs (cdboot), and stage 2; the boot area packs them with the
# kernel, and the ISO holds the boot area.
BOOT_CFLAGS := -std=gnu11 -ffreestanding -Os -Wall -Wextra -fno-pic \
               -fno-asynchronous-unwind-tables $(ARCHFLAGS) -Iboot -DMANIOS_VERSION='"$(VERSION)"'
MBR := $(BUILD)/boot/mbr.bin
CDBOOT := $(BUILD)/boot/cdboot.bin
STAGE2 := $(BUILD)/boot/stage2.bin
BOOTAREA := $(BUILD)/manios.bin
ISO := $(BUILD)/manios.iso
KERNEL_STRIPPED := $(BUILD)/manios-zkt.stripped.elf
DIST := dist

C_SOURCES := $(wildcard zkt/arch/i386/*.c zkt/drivers/*.c zkt/fs/*.c zkt/kernel/*.c \
                          zkt/mm/*.c zkt/net/*.c zkt/scheduler/*.c)
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
               -ffunction-sections -fdata-sections \
               -DMANIOS_VERSION='"$(VERSION)"' \
               -Wall -Wextra -MMD -MP $(ARCHFLAGS) -Ilibc/include -Izkt/abi -Idesktop/libgfx \
               -Idesktop/libwin -Iboot -Ithird_party/openbsd/include
USER_LDFLAGS := -nostdlib -static -T userland/user.ld -Wl,-n -Wl,--gc-sections

LIBC := $(BUILD)/libc/libc.a
CRT0 := $(BUILD)/libc/crt0.o
# Programs and C library parts from OpenBSD (third_party/openbsd/README.md,
# third_party/THIRD_PARTY_NOTICES.md), built unmodified against ManiOS's
# libc. Upstream builds them without -Wextra's style warnings.
OBSD := third_party/openbsd
# Each directory under usr.bin/ and bin/ is a program, named after it and
# built from all its .c files; OBSD_EXTRA adds a program's own flags.
OBSD_TOOL_DIRS := $(wildcard $(OBSD)/usr.bin/* $(OBSD)/bin/*)
OBSD_TOOLS := $(notdir $(OBSD_TOOL_DIRS))
OBSD_CFLAGS = $(USER_CFLAGS) -I$(OBSD)/sys -I$(OBSD)/lib/libutil -Wno-sign-compare \
              -Wno-unused-parameter -Wno-maybe-uninitialized -Wno-unused-but-set-variable \
              -Wno-type-limits -Wno-pointer-sign -Wno-implicit-fallthrough \
              -Wno-missing-field-initializers \
              '-DDEF_WEAK(x)=' $(OBSD_EXTRA)
# regexec.c includes engine.c, which isn't built on its own. (ohash, from
# OpenBSD's libutil, goes in libc too: a program links only what it uses.)
LIBC_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard libc/*.c) \
                  $(filter-out %/engine.c,$(wildcard $(OBSD)/lib/libc/*/*.c $(OBSD)/lib/libutil/*.c)))
# The 2D graphics library (M11); linked into every program, which only
# takes what it uses.
LIBGFX := $(BUILD)/desktop/libgfx/libgfx.a
LIBGFX_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard desktop/libgfx/*.c))
# The desktop (M12, docs/desktop/DESIGN.md): the window library, the
# compositor (desktop/wm/, one program) and the applications
# (desktop/apps/, one per file), all installed in /bin.
LIBWIN := $(BUILD)/desktop/libwin/libwin.a
LIBWIN_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard desktop/libwin/*.c))
USER_LIBS := $(LIBWIN) $(LIBGFX) $(LIBC)
WM_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard desktop/wm/*.c))
DESKTOP_APPS := $(patsubst desktop/apps/%.c,%,$(wildcard desktop/apps/*.c))
DESKTOP_BOOTFS := $(patsubst %,$(BUILD)/bootfs/bin/%,$(DESKTOP_APPS))
USER_PROGRAMS := $(patsubst userland/%.c,%,$(wildcard userland/bin/*.c userland/test/*.c)) \
                 $(patsubst %,bin/%,$(OBSD_TOOLS))
USER_OBJECTS := $(patsubst %,$(BUILD)/userland/%.o,$(USER_PROGRAMS))
# The licenses of the code from BSD go with the programs built from it.
NOTICES := $(BUILD)/bootfs/etc/notices
BOOTFS_FILES := $(patsubst %,$(BUILD)/bootfs/%,$(USER_PROGRAMS)) \
                $(patsubst userland/%,$(BUILD)/bootfs/%,$(wildcard userland/etc/*)) \
                $(BUILD)/bootfs/bin/desktop $(DESKTOP_BOOTFS) $(NOTICES)
BOOTFS_OBJECT := $(BUILD)/bootfs.o

.PHONY: all toolchain run test clean iso release test-images

all: $(KERNEL) $(ISO)

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

# The version is compiled in where it is shown.
$(BUILD)/zkt/kernel/main.o $(BUILD)/desktop/apps/about.o $(BUILD)/boot/stage2_entry.o \
    $(BUILD)/boot/stage2.o: VERSION

$(BUILD)/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/libc/%.o: libc/%.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/$(OBSD)/%.o: $(OBSD)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OBSD_CFLAGS) -c $< -o $@

# An OpenBSD program is linked where a userland/bin/ program would be, so
# the usual rules strip it and install it in /bin.
define OBSD_TOOL_RULE
$(BUILD)/userland/bin/$(notdir $(1)).elf: $(patsubst %.c,$(BUILD)/%.o,$(wildcard $(1)/*.c)) \
                                        $(CRT0) $(USER_LIBS) userland/user.ld
	$$(CC) $$(USER_LDFLAGS) -o $$@ $$(CRT0) $(patsubst %.c,$(BUILD)/%.o,$(wildcard $(1)/*.c)) \
	    $$(USER_LIBS)
endef
$(foreach d,$(OBSD_TOOL_DIRS),$(eval $(call OBSD_TOOL_RULE,$(d))))
# grep without zlib (-Z): OpenBSD's own switch for small builds.
$(BUILD)/$(OBSD)/usr.bin/grep/%.o: OBSD_EXTRA := -DNOZ

$(LIBC): $(LIBC_OBJECTS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/desktop/%.o: desktop/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(LIBGFX): $(LIBGFX_OBJECTS)
	@rm -f $@
	$(AR) rcs $@ $^

$(LIBWIN): $(LIBWIN_OBJECTS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/desktop/wm/desktop.elf: $(WM_OBJECTS) $(CRT0) $(USER_LIBS) userland/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ $(CRT0) $(WM_OBJECTS) $(USER_LIBS)

$(BUILD)/desktop/apps/%.elf: $(BUILD)/desktop/apps/%.o $(CRT0) $(USER_LIBS) userland/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ $(CRT0) $< $(USER_LIBS)

$(BUILD)/bootfs/bin/desktop: $(BUILD)/desktop/wm/desktop.elf
	@mkdir -p $(dir $@)
	$(STRIP) -o $@ $<

$(DESKTOP_BOOTFS): $(BUILD)/bootfs/bin/%: $(BUILD)/desktop/apps/%.elf
	@mkdir -p $(dir $@)
	$(STRIP) -o $@ $<

$(BUILD)/userland/%.o: userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/userland/%.elf: $(BUILD)/userland/%.o $(CRT0) $(USER_LIBS) userland/user.ld
	$(CC) $(USER_LDFLAGS) -o $@ $(CRT0) $< $(USER_LIBS)

$(BUILD)/bootfs/%: $(BUILD)/userland/%.elf
	@mkdir -p $(dir $@)
	$(STRIP) -o $@ $<

$(BUILD)/bootfs/etc/%: userland/etc/%
	@mkdir -p $(dir $@)
	cp $< $@

$(NOTICES): tools/mknotices.py $(shell find third_party -name '*.[ch]')
	@mkdir -p $(dir $@)
	python3 tools/mknotices.py $@

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

$(BUILD)/boot/%.o: boot/%.S boot/bootarea.h
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BUILD)/boot/%.o: boot/%.c boot/bootarea.h
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(MBR): $(BUILD)/boot/mbr.o boot/sector.ld
	$(LD) -nostdlib -T boot/sector.ld -o $@ $<
	@test $$(stat -c %s $@) -eq 446 || { echo "$@: not 446 bytes"; rm -f $@; exit 1; }

$(CDBOOT): $(BUILD)/boot/cdboot.o boot/sector.ld
	$(LD) -nostdlib -T boot/sector.ld -o $@ $<
	@test $$(stat -c %s $@) -eq 2048 || { echo "$@: not 2048 bytes"; rm -f $@; exit 1; }

$(STAGE2): $(BUILD)/boot/stage2_entry.o $(BUILD)/boot/stage2.o boot/stage2.ld
	$(LD) -nostdlib -T boot/stage2.ld -o $@ $(BUILD)/boot/stage2_entry.o $(BUILD)/boot/stage2.o

$(KERNEL_STRIPPED): $(KERNEL)
	$(STRIP) -o $@ $<

$(BOOTAREA): $(MBR) $(STAGE2) $(KERNEL_STRIPPED) tools/mkbootarea.py VERSION
	python3 tools/mkbootarea.py --mbr $(MBR) --stage2 $(STAGE2) --kernel $(KERNEL_STRIPPED) \
	    --version $(VERSION) -o $@

$(ISO): $(BOOTAREA) $(CDBOOT) $(MBR) tools/mkiso.py README.md LICENSE
	python3 tools/mkiso.py --bootarea $(BOOTAREA) --cdboot $(CDBOOT) --mbr $(MBR) \
	    --volume MANIOS_$(subst .,_,$(VERSION)) --file README.TXT=README.md \
	    --file LICENSE.TXT=LICENSE -o $@

iso: $(ISO)

# Test builds of the boot sector and stage 2 that use CHS disk reads,
# as on BIOSes without the LBA extensions (tests/install_test.py).
$(BUILD)/boot/chs/%.o: boot/%.S boot/bootarea.h
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -DFORCE_CHS -c $< -o $@

$(BUILD)/boot/chs/mbr.bin: $(BUILD)/boot/chs/mbr.o boot/sector.ld
	$(LD) -nostdlib -T boot/sector.ld -o $@ $<

$(BUILD)/boot/chs/stage2.bin: $(BUILD)/boot/chs/stage2_entry.o $(BUILD)/boot/stage2.o boot/stage2.ld
	$(LD) -nostdlib -T boot/stage2.ld -o $@ $(BUILD)/boot/chs/stage2_entry.o $(BUILD)/boot/stage2.o

$(BUILD)/manios-chs.bin: $(BUILD)/boot/chs/mbr.bin $(BUILD)/boot/chs/stage2.bin $(KERNEL_STRIPPED)
	python3 tools/mkbootarea.py --mbr $(BUILD)/boot/chs/mbr.bin --stage2 $(BUILD)/boot/chs/stage2.bin \
	    --kernel $(KERNEL_STRIPPED) --version $(VERSION) -o $@

test-images: $(ISO) $(BUILD)/manios-chs.bin

# What a release publishes (.github/workflows/release.yml).
release: $(ISO) $(KERNEL)
	@mkdir -p $(DIST)
	cp $(ISO) $(DIST)/manios-$(VERSION).iso
	cp $(KERNEL) $(DIST)/manios-$(VERSION)-kernel.elf
	cp $(NOTICES) $(DIST)/NOTICES.txt
	cd $(DIST) && sha256sum manios-$(VERSION).iso manios-$(VERSION)-kernel.elf NOTICES.txt > SHA256SUMS

run: $(KERNEL)
	tools/qemu-run.sh $(KERNEL)

test: $(KERNEL) test-images
	python3 tools/mkfont.py --check
	tests/boot_smoke_test.sh $(KERNEL)
	python3 tests/install_test.py $(BUILD)
	python3 tests/console_test.py $(KERNEL)
	python3 tests/openbsd_test.py $(KERNEL)
	python3 tests/net_test.py $(KERNEL)
	python3 tests/gfx_test.py $(KERNEL)
	python3 tests/desktop_test.py $(KERNEL)
	python3 tests/cluster_test.py $(KERNEL)

clean:
	rm -rf $(BUILD) $(DIST)

# Keep the unstripped programs for debugging (addr2line, objdump).
.SECONDARY: $(patsubst %,$(BUILD)/userland/%.elf,$(USER_PROGRAMS)) $(USER_OBJECTS) $(CRT0) \
            $(patsubst %,$(BUILD)/desktop/apps/%.elf,$(DESKTOP_APPS)) \
            $(patsubst %,$(BUILD)/desktop/apps/%.o,$(DESKTOP_APPS)) $(BUILD)/desktop/wm/desktop.elf

-include $(OBJECTS:.o=.d) $(LIBC_OBJECTS:.o=.d) $(CRT0:.o=.d) $(USER_OBJECTS:.o=.d) \
         $(LIBGFX_OBJECTS:.o=.d) $(LIBWIN_OBJECTS:.o=.d) $(WM_OBJECTS:.o=.d) \
         $(patsubst %,$(BUILD)/desktop/apps/%.d,$(DESKTOP_APPS))
