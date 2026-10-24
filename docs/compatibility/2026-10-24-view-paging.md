# VIEW 17.2 paging investigation — 2026-10-24

## Outcome and scope

Actual navigation of the original 35,362-byte VIEW.DOC is verified on a
fresh disposable FAT16 copy. This investigation fixed a general console
rendering defect. It did **not reproduce or establish the root cause of**
the previously reported pixel-identical screen at `18% of file read`.
Even before this change, the rebuilt checkout advanced its document, but
its physical display was corrupted. Do not attribute the old stall to this
fix or the signed-seek change without further evidence. VIEW remains Partial.

## Identified defect and minimal fix

The VGA console scrolled immediately when a character filled column 80 of
row 25. DOS text rendering fills that cell and then emits an ANSI cursor
position sequence. The physical console scrolled while the DOS video-memory
cache still described the unscrolled screen. Subsequent incremental redraws
left stale characters, shifted document lines and a missing filename header.
The before screenshot records this with lines from different pages mixed.

`zkt/drivers/vga_text.c` now defers wrap until a subsequent printable character.
ANSI cursor positioning/movement, CR, LF, backspace and clear reset pending
wrap. SGR preserves it. Tabs use a bounded number of spaces so deferring
wrap cannot add an extra tab stop. Ordinary continued text still wraps and
scrolls. Existing cursor-style/visibility edits in this file were preserved.
No DOS API implementation or CPU behavior was changed in this investigation.

## Keyboard, file and screen evidence

The [native trace](2026-10-24-view-api-trace.txt) was recorded by a separately
built diagnostic dosrun executable placed on a disposable image. A wrapper
around dos_int logs entry/return registers, INT 10h function counts and a
hash of the emulated B800 text page before each keyboard read. This wrapper
is outside the production checkout at `../view-paging-audit/trace.h`;
`qemu_trace.py` and `trace.py` there reproduce native/host investigations.
Production sources were not instrumented. Logging affects timing.

VIEW uses blocking enhanced BIOS reads, INT 16h/AH=10h. Three returned
AX=5100, then PgUp returned 4900 and Esc 011B. These are consumed application
reads, not just key arrivals. The focused queue test additionally verifies
that repeated AH=11h readiness calls preserve the event and AH=10h consumes
it once, advances the BIOS data-area head and leaves an empty queue.

| Screen before next key read | B800 hash | Video calls since preceding read |
| --- | --- | --- |
| Initial document | 352c7427 | AH02=5698, AH06=3, AH09=5489 (plus setup/probes) |
| After PgDn 1 | 7e95990e | AH02=825, AH03=28, AH06=1, AH09=768 |
| After PgDn 2 | 91bbb72b | AH02=727, AH03=28, AH06=1, AH09=670 |
| After PgDn 3 | 97b7ca20 | AH02=860, AH03=28, AH06=1, AH09=803 |
| After PgUp | 91bbb72b | AH02=727, AH03=28, AH06=1, AH09=670 |

The active BIOS display page stayed zero. VIEW redraws using BIOS cursor,
scroll and character services; changing emulated video-memory hashes agree
with those calls and physical screenshots. No unsupported CPU instruction
or DOS execution fault occurred. This does not measure VIEW's private paging
variables; the stronger evidence here is actual changed document content.

AH=3Dh returns valid handles 5 and 6. AH=42h END(0) returns DX:AX=0000:8A22
(35,362); SET(0) returns zero. Format detection reads two 512-byte chunks;
VIEW reopens the document for parsing, reads 500-byte chunks, a final
362-byte chunk and then zero at EOF. That parsing handle closes before the
first keyboard read. No additional open/read/seek calls occur during the
three forward pages or PgUp: this document has already been loaded. The
remaining handle 5 closes on exit.

The signed-seek fix was already integrated in this working tree. It was
left untouched; its focused test passes. All seek displacements in this
VIEW trace are zero, so this trace provides no evidence that negative
seeking caused the earlier paging failure.

## Actual document navigation

| Action | Visible lines | Content evidence |
| --- | --- | --- |
| Initial | 1–23 of 825 | Version 17.2 and package file list |
| PgDn | 24–46 | VIEWSEEK.EXE, HPZOOM.COM and contents |
| PgDn | 47–69 | THE VIEW SCREEN, THE ESC KEY and format detection |
| PgDn | 70–92 | Wordstar, Wordstar 2000 and Windows Write |
| PgUp | 47–69 | Entire physical VGA text page restored byte-for-byte |
| Esc | shell | “Thank you for using VIEW.”; no execution fault |

Screenshots: [before corruption](2026-10-24-view-before-pgdn-2.png),
[initial](2026-10-24-view-initial.png), [PgDn 1](2026-10-24-view-pgdn-1.png),
[PgDn 2](2026-10-24-view-pgdn-2.png), [PgDn 3](2026-10-24-view-pgdn-3.png),
[PgUp](2026-10-24-view-pgup.png). Physical VGA dumps and full serial logs are
also retained under `../view-paging-audit/after/` and `final/`.

## Files changed by this investigation

- `zkt/drivers/vga_text.c`: deferred wrapping only; preserves prior local edits.
- `tests/vga_wrap_test.py`: real console implementation with memory/IRQ/port
  stubs; bottom-right repaint, continued-text scroll, SGR, CR/LF, backspace,
  tabs and full-screen fill.
- `tests/dosrun_keyboard_queue_test.py`: enhanced BIOS PgDn queue regression.
- `tests/dosrun_view_test.py`: opt-in original-fixture integration test. Copies
  the supplied image into a temporary directory, checks the document hash,
  captures physical VGA memory/screenshots, checks three forward content
  changes, exact backward restoration and clean exit. Requires mtools/QEMU.
- `docs/compatibility/database.json` and `details.md`: VIEW evidence updated,
  Partial status retained; other existing edits preserved.
- This report, `2026-10-24-view-api-trace.txt`, and six `2026-10-24-view-*.png`
  screenshots linked above.

## Validation

- Native kernel rebuild: PASS (existing GNU-stack linker warning).
- VGA wrap unit regression: PASS. Reinstating immediate wrap in a temporary
  source copy fails the bottom-right screen-preservation assertion.
- Enhanced BIOS queue regression: PASS.
- Original-document VIEW navigation regression: PASS, including clean Esc.
- `dosrun_runtime_test.py`, `dosrun_port_test.py`, `dosrun_seek_test.py`: PASS.
- `dos_test.py` and `dosrun_test.py` against rebuilt kernel: PASS.
- `console_test.py`: PASS, including console colours and physical keyboard tests.
- Python syntax checks and `git diff --check`: PASS.

Reproduce navigation from the repository root:

```
python3 tests/dosrun_view_test.py ../dosprograms-audit/images/view.img build/manios-zkt.elf /tmp/view-evidence
python3 tests/vga_wrap_test.py
python3 tests/dosrun_keyboard_queue_test.py
```

Document SHA-256:
`965c30c2429809c90350c03f8151c4e9117591599804fe0e5b7de2143e831fd2`.
Final kernel SHA-256:
`c259c4babadff26cc8e5fe2840cd7dc54c45e833e673e70744efcdc43a6a2be5`.

All application runs used disposable FAT16 copies. Original fixtures were
only read. No commit, push, merge, publication or release artifact update.
Remaining work: reproduce the historical 18% stall with its precise binary,
build and timing; exercise long navigation to EOF, other document formats,
printing, searching and auxiliary utilities. Those features are not implied
by this basic paging result.
