# DOS decode and conversion utilities

Tested on ManiOS 0.26.1-10252026 in QEMU i386/486 with 64 MiB RAM. Every
program ran through `dosrun` with a disposable FAT16 test disk. The seven
archives came from the user-provided [COAST DOS decode archive](https://ftp.zx.net.nz/pub/archive/ftp.icm.edu.pl/pub/coast/msdos/decode/).

The shared 14-byte binary fixture was
`00 01 02 7F 80 FE FF 4D 61 6E 69 4F 53 0A` (SHA-256
`35a4534693ab0870750b7cf63b0cfad1dd42f909539fef52271e169389a2dbdc`).
Each result was copied from the FAT16 image or captured from standard output
and checked against an independent expected value.

| Program | Status | Verified operation |
| --- | --- | --- |
| COMHEX | Partial | Starts and reads a basename, but aborts with Pascal I/O error 99; leaves a zero-byte HEX file. |
| COMPHEX | Working | Equal files report zero mismatches; a one-byte difference is identified at `000D`. |
| HEXBIN | Working | Binary-to-Intel-HEX-to-binary round-trip matches all 14 bytes; record checksums validate. |
| ASCIFY 1.3 | Working | Encoded a 14-byte file to ASCIFY text and restored it byte-exactly. |
| FORMAT83 | Working | Emits valid Intel HEX to standard output; data and EOF checksums validate. |
| MAKEUUE 1.1 | Working | Creates a self-decoding BAT; its UU payload independently decodes to the exact fixture. The batch's DEBUG-based reconstruction was not run. |
| MIME64 Revision D | Working | Decodes a MIME Base64 attachment byte-exactly; the bundled sample ZIP passes CRC testing. |

## Individual results

### COMHEX

Archive SHA-256: `343311b6e0ce7bfc835a3aa6669d2ff57867cbde6aaee232c28f83717b22af77`.

Ran `dosrun C:/COMHEX.COM` and entered `TEST` at its prompt. Also tried the
explicit basename `C:/TEST`. Both attempts stopped with `I/O error 99` at
`PC=2FB7`; `TEST.HEX` existed but had zero bytes. The intended COM-to-HEX
conversion did not complete, so the result is Partial.

### COMPHEX

Archive SHA-256: `dabb361fc3cb4acc1f2b06594d6b242251f1f7b11e9e43ae12b6c92b7b9af100`.

Ran `dosrun C:/COMPHEX.EXE C:/A.HEX C:/SAME.HEX`; the summary reported byte
count `000E` and mismatch count `0000`. Compared `A.HEX` with `B.HEX`, where
the final byte alone differs: COMPHEX identified address `000D`, values `0A`
and `21`, and mismatch count `0001`.

### HEXBIN

Archive SHA-256: `27754202e267a1db8d4e2bda67a7eec5567c6380041c925c99db7330057bb7a7`.

Ran `dosrun C:/HEXBIN.EXE C:/TEST.BIN C:/OUT.HEX`, then converted `OUT.HEX`
back to `ROUND.BIN`. The generated 52-byte Intel HEX file contained:

```text
:0E0000000001027F80FEFF4D616E694F530AC2
:00000001FF
```

Each record checksum sums to zero modulo 256. `ROUND.BIN` matched the fixture
byte-for-byte (SHA-256 `35a4534693ab0870750b7cf63b0cfad1dd42f909539fef52271e169389a2dbdc`).

### ASCIFY 1.3

Archive SHA-256: `251d45708f57b7b4feb6ca332ed10bda4f251d42a34e40295bae699f620f9164`.

Ran `dosrun C:/ASCIFY.COM TEST.BIN TEST.ASC`; the 14-byte input became a
52-byte ASCII file. Removed the original fixture from the disposable disk and
ran `dosrun C:/ASCIFY.COM TEST.ASC`. The restored 14-byte file was an exact
match, including the binary bytes `80`, `FE` and `FF`.

### FORMAT83

Archive SHA-256: `abc93f762adabd0b0f62829f53e81b2c0cd887faccba73eba17516aa9da1b8df`.

Ran `dosrun C:/FORMAT83.COM C:/TEST.BIN` and captured its standard output,
which is the documented output destination. It emitted the same valid 14-byte
Intel HEX data record and EOF record shown in the HEXBIN section. Both checksums
validate. Redirecting output to `/n/ata0p1/OUT.HEX` was refused by the tested
shell path, so this check validates the stdout stream rather than a saved HEX
file.

### MAKEUUE 1.1

Archive SHA-256: `dac3b9a12316ed6d2921ff589b2cffdec36aa9fdad2ce3babbbecba5c226dc32`.

Ran `dosrun C:/MAKEUUE.EXE TEST.BIN TESTUU.BAT`. It created a 439-byte batch
file with the DEBUG bootstrap and the UU-encoded payload. Decoding that payload
using the format mapping documented in the package reconstructed the exact
14-byte fixture, followed by the format's one zero padding byte. The generated
batch was not run because DEBUG is not present in the tested ManiOS image; this
does not affect the verified encoder result.

### MIME64 Revision D

Archive SHA-256: `75c6919f12bd20fe8121acf9e1807a548627efa4a9a5bf56a9247dc5b41d7d14`.

Created a MIME attachment with a correct Base64 representation of the binary
fixture and a `name=TEST.BIN` header, then ran `dosrun C:/MIME64.EXE C:/INPUT.TXT`.
The extracted `TEST.BIN` matched all 14 input bytes exactly. The package's own
example was decoded with `dosrun C:/MIME64.EXE C:/MIME.64 C:/MIME.ZIP`; `unzip
-t` reported its `mime.txt` member valid with no CRC errors.
