#!/usr/bin/env python3
"""Collects the copyright notices and licenses of the code imported from
BSD projects (third_party/) into one text file, which the build puts in
the boot archive as /boot/etc/notices: the licenses require the notices
to go with the programs built from that code, not only with its source.

Each file's notice is everything above the note ManiOS adds under it
("ManiOS: imported from ..."), verbatim; a file without that note is an
error, so an import cannot skip it (FOUNDING-PROPOSAL §3.3, step 4).

Usage: tools/mknotices.py OUTPUT
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
THIRD_PARTY = os.path.join(ROOT, "third_party")
NOTE = "/*\n * ManiOS: imported from "

HEADER = """\
Notices for the software from other projects in ManiOS

ManiOS includes programs and C library functions from OpenBSD. Their
copyright notices and licenses follow, as they appear at the top of each
source file (third_party/ in the ManiOS source; the ledger is
third_party/THIRD_PARTY_NOTICES.md).
"""


def sources():
    for top, dirs, files in os.walk(THIRD_PARTY):
        dirs.sort()
        for name in sorted(files):
            if name.endswith((".c", ".h")):
                yield os.path.join(top, name)


def notice(path):
    with open(path, encoding="latin-1") as f:  # bytes as they are
        text = f.read()
    end = text.find(NOTE)
    if end <= 0:
        sys.exit(f"mknotices: {os.path.relpath(path, ROOT)}: no ManiOS import note "
                 "under its license header")
    return text[:end].rstrip() + "\n"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    parts = [HEADER]
    for path in sources():
        rel = os.path.relpath(path, ROOT)
        parts.append(f"\n{'=' * 72}\n{rel}\n{'=' * 72}\n\n{notice(path)}")
    if len(parts) == 1:
        sys.exit("mknotices: nothing under third_party/")
    with open(sys.argv[1], "w", encoding="latin-1") as f:
        f.write("".join(parts))


if __name__ == "__main__":
    main()
