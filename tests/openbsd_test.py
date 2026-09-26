#!/usr/bin/env python3
"""Tests of the tools imported from OpenBSD (third_party/openbsd/).

Boots the kernel in QEMU with a FAT disk of small text files, runs each
tool in the shell, and compares its whole output -- standard output and
error messages -- with what it must be. The expected outputs are
OpenBSD's behaviour: GNU's tools give the same for these cases, except
`uniq -c`'s column width. Also checks the parts of ManiOS's libc the
tools depend on: getopt(), err()/warn() with the program's name,
strtonum(), getline(), and that a program writing to a pipe whose
reader has gone ends (`yes | head`), as SIGPIPE would end it on Unix.
And that the licenses go with the programs: /boot/etc/notices holds
every imported file's copyright notice and license.

Usage: tests/openbsd_test.py [path-to-kernel-elf]
"""
import os
import shutil
import subprocess
import sys
import tempfile

from console_test import (DISK_SECTORS, PART_SECTORS, PART_START, PROMPT, SHELL_PROMPT,
                          Machine, TestFailure, fnv1a, mbr_entry)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTE = "/*\n * ManiOS: imported from "

FILES = {
    "LINES.TXT": "".join(f"{w}\n" for w in
                         "one two three four five six seven eight nine ten eleven twelve".split()),
    "WORDS.TXT": "hello world\nManiOS\n",
    "DUPS.TXT": "a\na\nb\nc\nc\nc\nd\n",
    "LONG.TXT": "the quick brown fox jumps over the lazy dog\n",
    "FIELDS.TXT": "root:x:0:0\nuser:x:1000:1000\n",
    "A.TXT": "apple\nbanana\ncherry\n",
    "B.TXT": "banana\ncherry\ndate\n",
    "TABS.TXT": "a\tb\tc\n1\t22\t333\n",
    "TEXT.TXT": "The quick brown fox\njumps over\nthe lazy dog.\n\nA second paragraph here.\n",
    "SPACES.TXT": "        eight\n    four    x\n",
    "DEPS.TXT": "a b\nb c\nc d\n",
    "J1.TXT": "1 apple\n2 banana\n3 cherry\n",
    "J2.TXT": "1 red\n3 yellow\n4 green\n",
    "CTRL.TXT": "tab\there\x01bell\x7f\n",
    "DIR/SUB.TXT": "a cherry in a subdirectory\n",
    "DIR/DIR/DEEP.TXT": "a deep cherry\n",  # a directory named like its parent
}

CUT_USAGE = ("usage: cut -b list [-n] [file ...]\n"
             "       cut -c list [file ...]\n"
             "       cut -f list [-s] [-d delim] [file ...]\n")

# (command, its whole output). The shell runs in the disk's directory.
SHELL_CASES = [
    ("head -n 3 lines.txt", "one\ntwo\nthree\n"),
    ("head -2 lines.txt", "one\ntwo\n"),
    ("head -n 1 a.txt b.txt", "==> a.txt <==\napple\n\n==> b.txt <==\nbanana\n"),
    ("cat lines.txt | head -n 2", "one\ntwo\n"),
    ("rev words.txt", "dlrow olleh\nSOinaM\n"),
    ("uniq dups.txt", "a\nb\nc\nd\n"),
    ("uniq -c dups.txt", "   2 a\n   1 b\n   3 c\n   1 d\n"),
    ("uniq -d dups.txt", "a\nc\n"),
    ("uniq -u dups.txt", "b\nd\n"),
    ("fold -w 10 long.txt", "the quick \nbrown fox \njumps over\n the lazy \ndog\n"),
    ("fold -s -w 10 long.txt", "the quick \nbrown fox \njumps \nover the \nlazy dog\n"),
    ("cut -d : -f 1,3 fields.txt", "root:0\nuser:1000\n"),
    ("cut -d: -f2- fields.txt", "x:0:0\nx:1000:1000\n"),
    ("cut -c 2-3 a.txt", "pp\nan\nhe\n"),
    ("cut -f 2 tabs.txt", "b\n22\n"),
    ("comm a.txt b.txt", "apple\n\t\tbanana\n\t\tcherry\n\tdate\n"),
    ("comm -12 a.txt b.txt", "banana\ncherry\n"),
    ("expand tabs.txt", "a       b       c\n1       22      333\n"),
    ("expand -t 4 tabs.txt", "a   b   c\n1   22  333\n"),
    ("paste a.txt b.txt", "apple\tbanana\nbanana\tcherry\ncherry\tdate\n"),
    ("paste -s -d , a.txt", "apple,banana,cherry\n"),
    ("paste -d : a.txt - < b.txt", "apple:banana\nbanana:cherry\ncherry:date\n"),
    ("basename /a/b/c.txt .txt", "c\n"),
    ("basename /usr/bin/", "bin\n"),
    ("dirname /a/b/c.txt", "/a/b\n"),
    ("dirname c.txt", ".\n"),
    # yes(1) writes until its reader has gone; then stdio ends it.
    ("yes | head -n 3", "y\ny\ny\n"),
    ("yes ManiOS | head -n 2", "ManiOS\nManiOS\n"),
    # Errors: getopt(), err()/warn() and strtonum() messages.
    ("head -n x lines.txt", "head: count is invalid: x\n"),
    ("head -z", "head: unknown option -- z\nusage: head [-count | -n count] [file ...]\n"),
    ("head nosuch", "head: nosuch: no such file or directory\n"),
    ("cut -f 0 fields.txt", "cut: [-bcf] list: 0 too small (allowed 1-2048)\n"),
    ("cut -d", "cut: option requires an argument -- d\n" + CUT_USAGE),
    ("basename", "usage: basename string [suffix]\n"),
    ("comm a.txt", "usage: comm [-123f] file1 file2\n"),
    # 0.17: regular expressions (OpenBSD's regex library in libc).
    ("grep an a.txt", "banana\n"),
    ("grep -c e lines.txt", "9\n"),
    ("grep -v e lines.txt", "two\nfour\nsix\n"),
    ("grep -n '^t' lines.txt", "2:two\n3:three\n10:ten\n12:twelve\n"),
    ("grep -i MANIOS words.txt", "ManiOS\n"),
    ("grep -E 'one|two' lines.txt", "one\ntwo\n"),
    ("grep -w an a.txt", ""),
    ("grep -l banana a.txt b.txt", "a.txt\nb.txt\n"),
    ("cat lines.txt | grep ve", "five\nseven\neleven\ntwelve\n"),
    ("grep -F . text.txt", "the lazy dog.\nA second paragraph here.\n"),
    ("grep -r cherry dir", "dir/dir/deep.txt:a deep cherry\ndir/sub.txt:a cherry in a subdirectory\n"),
    ("yes | grep -m 2 y", "y\ny\n"),
    ("grep '[' lines.txt", "grep: brackets ([ ]) not balanced\n"),
    ("grep x nosuch", "grep: nosuch: no such file or directory\n"),
    ("sed s/a/A/g a.txt", "Apple\nbAnAnA\ncherry\n"),
    ("sed -n 2p lines.txt", "two\n"),
    ("sed 3q lines.txt", "one\ntwo\nthree\n"),
    ("sed /e/d lines.txt", "two\nfour\nsix\n"),
    ("sed -E 's/(an)+/X/' a.txt", "apple\nbXa\ncherry\n"),
    ("sed 's/\\(.*\\)/<\\1>/' a.txt", "<apple>\n<banana>\n<cherry>\n"),
    ("sed y/abc/ABC/ a.txt", "Apple\nBAnAnA\nCherry\n"),
    ("echo hello | sed 's/l*o/0/'", "he0\n"),
    ("sed = a.txt", "1\napple\n2\nbanana\n3\ncherry\n"),
    ("sed -i s/a/b/ a.txt", "sed: a.txt: read-only file system\n"),
    ("nl a.txt", "     1\tapple\n     2\tbanana\n     3\tcherry\n"),
    ("nl -b a text.txt", "     1\tThe quick brown fox\n     2\tjumps over\n     3\tthe lazy dog.\n"
                         "     4\t\n     5\tA second paragraph here.\n"),
    ("expr 1 + 2", "3\n"),
    ("expr 7 % 3", "1\n"),
    ("expr -9 / 2", "-4\n"),
    ("expr 4294967296 / 3", "1431655765\n"),  # 64-bit division, on a 386
    ("expr 9223372036854775807 + 1", "expr: overflow\n"),
    ("expr abc : 'a\\(.\\)'", "b\n"),
    ("expr 5 '>' 3", "1\n"),
    ("expr 1 / 0", "expr: division by zero\n"),
    # 0.17: the other text tools.
    ("echo hello | tr a-z A-Z", "HELLO\n"),
    ("tr -d aeiou < a.txt", "ppl\nbnn\nchrry\n"),
    ("echo aaabbb | tr -s ab", "ab\n"),
    ("echo hello | tr '[:lower:]' '[:upper:]'", "HELLO\n"),
    ("colrm 4 10 < long.txt", "thebrown fox jumps over the lazy dog\n"),
    ("fmt -w 20 text.txt", "The quick brown fox\njumps over the lazy\ndog.\n\nA second paragraph\nhere.\n"),
    ("join j1.txt j2.txt", "1 apple red\n3 cherry yellow\n"),
    ("join -a 1 j1.txt j2.txt", "1 apple red\n2 banana\n3 cherry yellow\n"),
    ("lam a.txt -s : b.txt", "apple:banana\nbanana:cherry\ncherry:date\n"),
    ("unexpand spaces.txt", "\teight\n    four    x\n"),
    ("echo hi | tee /dev/null", "hi\n"),
    ("tee /n/ata0p1/new.txt < a.txt",
     "tee: /n/ata0p1/new.txt: read-only file system\napple\nbanana\ncherry\n"),
    ("tail -n 3 lines.txt", "ten\neleven\ntwelve\n"),
    ("tail -3 lines.txt", "ten\neleven\ntwelve\n"),
    ("cat lines.txt | tail -n 2", "eleven\ntwelve\n"),  # a pipe: ESPIPE
    ("tail -r a.txt", "cherry\nbanana\napple\n"),
    ("cat a.txt | tail -r", "cherry\nbanana\napple\n"),
    ("tail -c 7 lines.txt", "twelve\n"),
    ("tail -n +11 lines.txt", "eleven\ntwelve\n"),
    ("tail -f a.txt", "tail: kqueue: function not implemented\napple\nbanana\ncherry\n"
                      "tail: Unable to follow a.txt: bad file descriptor\n"),
    ("cmp a.txt a.txt", ""),
    ("cmp a.txt b.txt", "a.txt b.txt differ: char 1, line 1\n"),
    ("column -t -s : fields.txt", "root  x  0     0\nuser  x  1000  1000\n"),
    ("tsort deps.txt", "a\nb\nc\nd\n"),
    ("vis ctrl.txt", "tab\there\\^Abell\\^?\n"),
    ("vis ctrl.txt | unvis | vis", "tab\there\\^Abell\\^?\n"),
    ("col < a.txt", "apple\nbanana\ncherry\n"),
]

# Exit statuses, from the kernel monitor's `run` (which says nothing
# when a program exits with status 0).
MONITOR_CASES = [
    ("run /bin/head -n 1 /n/ata0p1/a.txt", "apple\n"),
    ("run /bin/head -n x /n/ata0p1/a.txt",
     "head: count is invalid: x\nrun: /bin/head exited with status 1\n"),
    ("run /bin/cut", CUT_USAGE + "run: /bin/cut exited with status 1\n"),
    ("run /bin/head /n/ata0p1/nosuch",
     "head: /n/ata0p1/nosuch: no such file or directory\n"
     "run: /bin/head exited with status 1\n"),
    ("run /bin/test -f /n/ata0p1/a.txt", ""),
    ("run /bin/test -d /n/ata0p1/a.txt", "run: /bin/test exited with status 1\n"),
    ("run /bin/test -d /n/ata0p1/dir", ""),
    ("run /bin/test -e /n/ata0p1/nosuch", "run: /bin/test exited with status 1\n"),
    ("run /bin/test abc = abc", ""),
    ("run /bin/test 3 -lt 2", "run: /bin/test exited with status 1\n"),
    ("run /bin/test /n/ata0p1/a.txt -ef /n/ata0p1/dir/../a.txt", ""),
    ("run /bin/test /n/ata0p1/a.txt -ef /n/ata0p1/b.txt", "run: /bin/test exited with status 1\n"),
    ("run /bin/cmp -s /n/ata0p1/a.txt /n/ata0p1/b.txt", "run: /bin/cmp exited with status 1\n"),
    ("run /bin/grep -q an /n/ata0p1/a.txt", ""),
    ("run /bin/grep -q zzz /n/ata0p1/a.txt", "run: /bin/grep exited with status 1\n"),
]


def make_disk(path):
    img = bytearray(DISK_SECTORS * 512)
    img[446:462] = mbr_entry(0x00, 0x06, PART_START, PART_SECTORS)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)
    part = f"{path}@@{PART_START * 512}"
    subprocess.run(["mformat", "-i", part, "-T", str(PART_SECTORS), "-h", "16", "-s", "63",
                    "-H", str(PART_START), "::"], check=True)
    files = os.path.join(os.path.dirname(path), "files")
    os.mkdir(files)
    subprocess.run(["mmd", "-i", part, "::/DIR", "::/DIR/DIR"], check=True)
    for name, text in FILES.items():
        local = os.path.join(files, name.replace("/", "_"))
        with open(local, "w", encoding="latin-1") as f:
            f.write(text)
        subprocess.run(["mcopy", "-i", part, local, "::/" + name], check=True)


def notices_cases(kernel):
    """/boot/etc/notices must carry every imported file's license header
    (everything above ManiOS's note), and be in the running system
    whole."""
    path = os.path.join(os.path.dirname(kernel), "bootfs", "etc", "notices")
    with open(path, "rb") as f:
        data = f.read()
    text = data.decode("latin-1")  # some headers are ISO 8859-1: compare bytes
    count = 0
    for top, _, names in os.walk(os.path.join(ROOT, "third_party")):
        for name in names:
            if name.endswith((".c", ".h")):
                with open(os.path.join(top, name), encoding="latin-1") as f:
                    source = f.read()
                header = source[:source.find(NOTE)].rstrip()
                if NOTE not in source or header not in text:
                    raise TestFailure(f"{name}: its license header is not in {path}")
                count += 1
    print(f"PASS: /boot/etc/notices has the license headers of all {count} imported files")
    return [("sum /boot/etc/notices",
             f"/boot/etc/notices: {len(data)} bytes, fnv1a {fnv1a(data):08x}\n"),
            ("head -n 1 /boot/etc/notices",
             "Notices for the software from other projects in ManiOS\n")]


def run_case(m, command, want, prompt, how):
    m.type_serial(command + "\r")
    out = m.expect(prompt).replace("\r\n", "\n")
    got = out[len(command) + 1:] if out.startswith(command + "\n") else out
    if how == "whole" and got != want:
        raise TestFailure(f"{command!r} gave {got!r}, not {want!r}")
    if how == "contains" and want not in got:
        raise TestFailure(f"{command!r} gave {got!r}, without {want!r}")


def run_cases(m, cases, prompt, how):
    failures = 0
    for command, want in cases:
        try:
            run_case(m, command, want, prompt, how)
            print(f"PASS: {command!r}")
        except TestFailure as e:
            print(f"FAIL: {e}")
            failures += 1
            m.mark = len(m.output)
            m.type_serial("\r")
            m.expect(prompt)
    return failures


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    workdir = tempfile.mkdtemp(prefix="zkt-openbsd-")
    failures = 0
    m = None
    try:
        disk = os.path.join(workdir, "disk.img")
        make_disk(disk)
        m = Machine(kernel, ["-drive", f"file={disk},format=raw,if=ide"])
        m.expect(SHELL_PROMPT, timeout=90)
        run_case(m, "cd /n/ata0p1", "", SHELL_PROMPT, "whole")
        failures += run_cases(m, SHELL_CASES + notices_cases(kernel), SHELL_PROMPT, "whole")
        run_case(m, "exit", "console: the shell has exited", PROMPT, "contains")
        failures += run_cases(m, MONITOR_CASES, PROMPT, "whole")
    except TestFailure as e:
        print(f"FAIL: {e}")
        failures += 1
    finally:
        if m:
            m.close()
        shutil.rmtree(workdir, ignore_errors=True)
    print(f"openbsd_test: {'FAILED' if failures else 'all passed'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
