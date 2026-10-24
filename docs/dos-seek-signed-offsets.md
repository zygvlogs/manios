# DOS signed seek regression (2026-10-24)

AH=42h assembled CX:DX into uint32_t and widened directly to long. On
64-bit hosts, FFFF:FFFF became +4294967295 rather than -1. The handler now
casts to int32_t before passing the displacement to lseek for SET, CUR or
END. Successful positions return DX:AX with carry clear. Invalid origins
return error 1, invalid handles error 6, and negative positions, overflow
beyond UINT32_MAX and other host seek failures error 25, with carry set.
Previously invalid origins selected END and host failures returned error 2.

Changed implementation: userland/dosrun/dosapi.c (AH=42h only).
Added tests/dosrun_seek_test.py using the existing host harness and temporary
files. Covers SET zero/positive, CUR positive/negative, END zero/-1/-6,
reading the final byte through AH=3Fh, before-start errors, invalid handles,
invalid origins, high-word results and overflow. No original fixtures modified.

Validation: focused seek suite passes. Existing host, exec, port, runtime and
string suites pass. PIT suite fails at its existing CPU instruction assertion
(cpu_step()==CPU_FAULT, IP/AX/flags); its preceding PIT checks pass. Native
make -j4 succeeds (linker executable-stack warnings). Initial DOS and ManiDOS
QEMU suites pass; rerun after rebuilding recorded separately below. These
suites create disposable disk images, including FAT16 volumes.

Compatibility limits: host lseek governs positioning. Seeking before zero
returns an immediate error rather than retaining a negative DOS position.
The native host uses signed 32-bit long and cannot represent positive file
positions above LONG_MAX; the 64-bit harness checks the DOS UINT32_MAX bound.
A successful host seek beyond the DOS range reports an error but leaves the
host cursor at that position. No VIEW document paging was independently
verified and its compatibility status has not been changed. No commits,
pushes, merges or publication performed.

Post-build QEMU validation: tests/dos_test.py reports all passed;
tests/dosrun_test.py reports OK: 0 failures. Logs are in
/tmp/seek-manidos-rebuilt.log and /tmp/seek-dosrun-rebuilt.log.
