# VIEW and Life2 focused follow-up — 2026-10-24

## VIEW: document traversal and search pass

The opt-in `tests/dosrun_view_test.py` now accepts `--extended` after the
image, kernel and evidence directory arguments. It tests the original
35,362-byte VIEW.DOC on a temporary FAT16 image copy.

The extended run passed:

- Home restored the initial page exactly.
- 35 PgDn presses advanced through the document without backward movement,
  reaching the visible `[END OF DOCUMENT]` marker. The last PgDn screen
  reports lines 806–824 of 825; a further PgDn stays there. End reports
  806–825, including the trailing blank line. The test uses the actual EOF
  marker rather than assuming the final nonempty line must be number 825.
- End displayed the final document section.
- 35 PgUp presses returned to the initial page, exactly matching its text.
- F2 searched for `bookmarks`; a second F2 displayed a different match.
- Esc printed the exit message and returned to the shell without a fault.

Evidence: [all navigation spans](2026-10-24-view-full-navigation.json),
[Home](2026-10-24-view-full-home.png), [End](2026-10-24-view-full-end.png),
[first search](2026-10-24-view-full-search-result.png),
[next match](2026-10-24-view-full-search-next.png).
Raw VGA snapshots, PPM screenshots and serial log are in
`../navigation-audit/view-full/`.

This verifies actual document content changes, not just percentages.
Printing, alternate file formats and the historic 18% stall remain outside
these successful checks. VIEW stays Partial at the whole-application level.

## Life2: autonomous RUN remains blocked; timer dependency identified

Using a fresh copy of `../games-audit/disks/life2.img`, pass both instruction
pages with Space, mark three adjacent cells with M, Right, M, Right, M,
then press R. The horizontal starting cells are (row,column), zero-based:
(11,39), (11,40), (11,41).

The native run did not complete an unattended horizontal → vertical →
horizontal cycle. During 160 physical VGA captures over approximately
40 seconds the three horizontal X cells remained visible. Keyboard input
subsequently caused partial redraws; those are not accepted as proof of
correct autonomous simulation. Screenshots:
[edited blinker](2026-10-24-life2-blinker-edit.png),
[RUN starting state](2026-10-24-life2-blinker-run.png).
The raw captures and script are in `../navigation-audit/`.

The host trace and disassembly identify a concrete dependency:

- The program directly installs its timer handler in IVT entry 08h and
  programs PIT channel 0 with a 2048 divisor.
- Its sound routine spins at 111E:0B9F–0BAB waiting for the word at DS:0043
  to become zero. The handler decrements this word and sends PIC EOI to 20h.
- The baseline host run remains at 111E:0BA6 after 30 million instructions,
  even when the BIOS data-area clock is advanced.
- ManiDOS models PIT counter values but currently provides no IRQ source;
  the master PIC mask implementation explicitly documents this limitation.
  Advancing BIOS clock memory alone cannot execute the application's handler.

In an isolated host experiment, synthesizing IRQ0 every 2000 instructions,
executing the installed handler, and accepting PIC EOI 20h makes the same
unmodified program alternate repeatedly between:

```
horizontal: (11,39) X, (11,40) X, (11,41) X
vertical:   (10,40) O, (11,40) O, (12,40) O
horizontal: (11,39) X, (11,40) X, (11,41) X
```

[Baseline and diagnostic trace](2026-10-24-life2-timer-trace.txt).
The application uses X/O for alternating generations. The experiment's IRQ
cadence is artificial; it proves the timer dependency, not native timing or
full hardware compatibility. Diagnostic code is retained as
`../navigation-audit/hosttrace.py` and `irqexperiment.py`. It does not patch
the original EXE or production compatibility code.

Life2 remains Partial. The next implementation task is general PIT IRQ0
scheduling, delivery to installed guest handlers and PIC acknowledgement,
with tests for IF/masking, interrupt return and timing. Merely swallowing
speaker ports or updating BIOS ticks will not fix this wait.

## Validation and changes

The extended VIEW integration test passed on the existing rebuilt kernel.
The first test attempt caught an overly strict EOF line-number assumption;
it was corrected to check document content. Python compilation, compatibility
JSON parsing and `git diff --check` pass. No production source changed in
this follow-up, so the previously passing kernel/DOS suites were not rerun.

Modified: `tests/dosrun_view_test.py` (extended traversal/search test),
`docs/compatibility/database.json` and `details.md` (updated observations).
Added: this report, `2026-10-24-view-full-navigation.json`, four
`2026-10-24-view-full-*.png` screenshots, two
`2026-10-24-life2-blinker-*.png` screenshots and the timer trace.

Reproduce VIEW from the repository root:

```
python3 tests/dosrun_view_test.py ../dosprograms-audit/images/view.img build/manios-zkt.elf /tmp/view-full-evidence --extended
```

Kernel SHA-256:
`c259c4babadff26cc8e5fe2840cd7dc54c45e833e673e70744efcdc43a6a2be5`.
LIFE2.EXE SHA-256:
`45f45ec0ff09ef9eb7f7a222b255d39ee263f6119467ec28e34a2f042760fd0a`.
Original Life2 FAT16 image SHA-256:
`33ccf1a8fdba0e9f7091c2944243586607dbfa0a62238dbf4ca16edfcd024683`.

All application runs used disposable FAT16 copies. Original fixtures were
preserved. No commit, push, merge, release update or publication performed.
