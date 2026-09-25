#!/usr/bin/env python3
"""Writes a raw hard disk image that boots ManiOS, laid out as the
installer (userland/bin/install.c) lays out a disk: an MBR with the boot
code from the boot area, and one active partition of type 0xDA at 1 MiB
holding the boot area, with the given kernel command line.

Usage: tools/mkdisk.py --bootarea AREA [--cmdline LINE] [--size MIB] -o OUT
"""
import argparse
import struct
import sys

SECTOR, START, ALIGN = 512, 2048, 2048


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bootarea", required=True)
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--size", type=int, default=16, help="MiB")
    ap.add_argument("-o", dest="out", required=True)
    a = ap.parse_args()
    area = bytearray(open(a.bootarea, "rb").read())
    if area[:8] != b"ZKTBOOT1":
        sys.exit("mkdisk: not a boot area")
    area[256:512] = a.cmdline.encode().ljust(256, b"\0")[:256]
    mbr_off, mbr_len = struct.unpack_from("<II", area, 16)
    sectors = (len(area) + SECTOR - 1) // SECTOR
    part = (sectors + ALIGN - 1) // ALIGN * ALIGN
    disk = bytearray(a.size * 1024 * 1024)
    if START + part > len(disk) // SECTOR:
        sys.exit("mkdisk: the disk is too small")
    disk[:446] = area[mbr_off:mbr_off + mbr_len]
    disk[446:462] = struct.pack("<B3sB3sII", 0x80, b"\xFE\xFF\xFF", 0xDA, b"\xFE\xFF\xFF", START, part)
    disk[510:512] = b"\x55\xAA"
    disk[START * SECTOR:START * SECTOR + len(area)] = area
    open(a.out, "wb").write(disk)


if __name__ == "__main__":
    main()
