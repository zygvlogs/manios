#!/usr/bin/env python3
"""Packs the ManiOS boot area (boot/bootarea.h): a header, the MBR boot
code, stage 2 and the kernel, each on a 2048-byte boundary, with a CRC-32
of everything after the header. The same bytes boot from a CD (the file
MANIOS.BIN) and from a hard disk (partition type 0xDA).

Usage: tools/mkbootarea.py --mbr MBR --stage2 STAGE2 --kernel ELF
                           --version V [--cmdline LINE] -o OUT
"""
import argparse
import struct
import sys
import zlib

MAGIC = b"ZKTBOOT1"
ALIGN = 2048
CMDLINE_OFFSET, CMDLINE_MAX = 256, 256


def pad(data):
    return data + b"\0" * (-len(data) % ALIGN)


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
    header = bytearray(ALIGN)
    header[0:8] = MAGIC
    struct.pack_into("<IIIIIIIII", header, 8, 1, total, mbr_off, len(mbr), stage2_off,
                     len(stage2), kernel_off, len(kernel), zlib.crc32(body))
    header[48:48 + 16] = version.encode().ljust(16, b"\0")[:16]
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
