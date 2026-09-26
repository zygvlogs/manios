#!/usr/bin/env python3
"""Packs the ManiOS boot area (boot/bootarea.h): a header, the MBR boot
code, stage 2 and the kernel, each on a 2048-byte boundary, with a CRC-32
of everything after the header. The same bytes boot from a CD (the file
MANIOS.BIN) and from a hard disk (partition type 0xDA). The header says
where stage 2 is to load the area: just past the kernel's memory, so
the kernel can be as big as it needs to be.

Usage: tools/mkbootarea.py --mbr MBR --stage2 STAGE2 --kernel ELF
                           --version V [--cmdline LINE] -o OUT
"""
import argparse
import struct
import sys
import zlib

MAGIC = b"ZKTBOOT1"
FORMAT = 2  # boot/bootarea.h: BOOTAREA_VERSION
ALIGN = 2048
CMDLINE_OFFSET, CMDLINE_MAX = 256, 256
LOAD_ADDR_OFFSET = 64
# The kernel's frame bitmap goes right after its image: room for one
# covering 4 GiB (a bit per page), so the boot area never has to move it.
BITMAP_ROOM = 4 * 1024 * 1024 * 1024 // 4096 // 8
AREA_MAX_SIZE = 16 * 1024 * 1024  # bootarea.h; the kernel's modules window


def pad(data):
    return data + b"\0" * (-len(data) % ALIGN)


def kernel_end(elf):
    """The physical address past the kernel's last loaded byte (.bss
    included), from its program headers."""
    phoff, = struct.unpack_from("<I", elf, 28)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    end = 0
    for i in range(phnum):
        p_type, _, _, paddr, _, memsz = struct.unpack_from("<IIIIII", elf, phoff + i * phentsize)
        if p_type == 1 and memsz:  # PT_LOAD
            end = max(end, paddr + memsz)
    if not end:
        sys.exit("mkbootarea: the kernel has no loadable segments")
    return end


def pack(mbr, stage2, kernel, version, cmdline=""):
    if len(mbr) != 446:
        sys.exit("mkbootarea: the MBR boot code must be 446 bytes")
    if not kernel.startswith(b"\x7fELF"):
        sys.exit("mkbootarea: the kernel is not an ELF file")
    if len(cmdline.encode()) >= CMDLINE_MAX:
        sys.exit("mkbootarea: the command line is too long")
    mbr_off = ALIGN
    stage2_off = mbr_off + len(pad(mbr))
    kernel_off = stage2_off + len(pad(stage2))
    body = pad(mbr) + pad(stage2) + pad(kernel)
    total = ALIGN + len(body)
    if total > AREA_MAX_SIZE:
        sys.exit(f"mkbootarea: the boot area ({total} bytes) is over the kernel's "
                 f"{AREA_MAX_SIZE >> 20} MiB modules window")
    # Stage 2 puts the whole area just past the kernel and its bitmap.
    load_addr = (kernel_end(kernel) + BITMAP_ROOM + 0xFFFF) & ~0xFFFF
    header = bytearray(ALIGN)
    header[0:8] = MAGIC
    struct.pack_into("<IIIIIIIII", header, 8, FORMAT, total, mbr_off, len(mbr), stage2_off,
                     len(stage2), kernel_off, len(kernel), zlib.crc32(body))
    header[48:48 + 16] = version.encode().ljust(16, b"\0")[:16]
    struct.pack_into("<I", header, LOAD_ADDR_OFFSET, load_addr)
    header[CMDLINE_OFFSET:CMDLINE_OFFSET + len(cmdline)] = cmdline.encode()
    return bytes(header) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mbr", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--cmdline", default="")
    ap.add_argument("-o", dest="out", required=True)
    a = ap.parse_args()
    read = lambda path: open(path, "rb").read()
    area = pack(read(a.mbr), read(a.stage2), read(a.kernel), a.version, a.cmdline)
    with open(a.out, "wb") as f:
        f.write(area)


if __name__ == "__main__":
    main()
