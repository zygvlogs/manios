# M11 — Graphics: Framebuffer and 2D Primitives

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §6](../FOUNDING-PROPOSAL.md#6-development-roadmap)
milestone M11: the foundation the desktop (M12) draws on.

```
manios% gfxdemo            # 640x480, 32-bit colour
manios% gfxdemo 800 600
manios% gfxdemo vga        # 320x200, 256 colours: any VGA card
manios% cat /dev/fbctl
text
```

## What M11 delivers

| Piece | Files | Summary |
|---|---|---|
| PCI | `zkt/drivers/pci.c` | Configuration mechanism #1; scan of every bus; monitor `pci` |
| Bochs VBE | `zkt/drivers/fb.c` | Linear framebuffer at any size up to 1600x1200, 32-bit, via the DISPI registers and the PCI BAR |
| VGA mode 13h | `zkt/drivers/vga_hw.c` | 320x200, one RGB 3-3-2 byte per pixel, on any VGA card |
| Text state | `vga_hw.c`, `vga_text.c` | Registers, palette and font saved and restored around graphics; text kept in a shadow meanwhile |
| Devices | `fb.c` | `/dev/fbctl` (mode control), `/dev/fb` (pixels at byte offsets) |
| `seek` | `SYS_SEEK`, `lseek`, `fseek`/`ftell`/`rewind` | Offsets for `/dev/fb`; also fixes M9's "no seeking" |
| libgfx | `desktop/libgfx/` | Canvases, clipping, rectangles, lines, circles, gradients, blending, blits, text, screen output |
| Font | `desktop/libgfx/font.txt`, `tools/mkfont.py` | An original 5x9 bitmap font for printable ASCII |
| Programs | `userland/bin/gfxdemo.c`, `userland/test/gtest.c`, `fbtest.c` | Demo; libgfx and device conformance tests |

## Design decisions

- **Two display paths:**
  - **Bochs VBE** ("BGA", what QEMU's default `-vga std`, Bochs and
    VirtualBox emulate). It needs no BIOS calls: the mode is set through
    I/O ports 0x1CE/0x1CF, and the framebuffer is found at the display
    adapter's PCI BAR, then mapped at a fixed kernel address
    (`KERNEL_FB_START`, 16 MiB).
  - **VGA mode 13h**, programmed register by register. Every VGA card
    since 1987 shows it, so a real 386 or 486 with an ISA VGA card gets
    graphics too. Its palette is set so each pixel byte *is* its colour
    in RGB 3-3-2.

  VESA BIOS modes were left out: calling the BIOS means real mode or
  v8086 mode, a large piece of machinery for one feature.
- **Text mode survives graphics.** Graphics modes use the video memory
  where text mode keeps its characters and its font; a Bochs VBE mode
  overwrites both. So on the way in, the kernel saves:
  - the VGA registers (all readable on a VGA);
  - the DAC palette;
  - the font in plane 2.

  While graphics are up, the text driver writes into a shadow copy of
  the screen, so console output isn't lost. On the way out, everything
  is put back. A kernel panic switches to text first, without locking,
  so its message is always visible.
- **The kernel exposes pixels, not drawing.**
  - `/dev/fbctl` reads `text`, or `WIDTH HEIGHT DEPTH PITCH FORMAT
    DRIVER` (e.g. `640 480 32 2560 xrgb8888 bga`).
  - Writing `mode W H`, `mode vga` or `text` switches modes.
  - `/dev/fb` holds the pixels, read and written at byte offsets
    (`y * PITCH + x * bytes-per-pixel`).

  Drawing happens in userspace, in libgfx. Plan 9 put drawing in the
  kernel (`/dev/draw`); on a single-CPU 386, keeping the kernel small
  matters more, and a program draws into its own memory at full speed
  anyway. Positioned I/O needed an offset, so the ABI gained `SYS_SEEK`
  (a new call under version 1; see M9's versioning rule). The ABI also
  gained optional `pread`/`pwrite`/`size` operations for character
  devices.
- **libgfx draws into a 32-bit canvas** (0xAARRGGBB). `gfx_present`
  converts when writing to the screen.
  - For 256-colour VGA it applies 4x4 ordered dithering, so a gradient
    becomes a texture rather than bands. The dither offset stays below
    one quantisation step, so pure colours come out exact.
  - All clipping happens in one place (`gfx_intersect`).
  - Blending is "source over", with an exact divide-by-255 done with
    shifts.
  - Nothing uses floating point. A 386 may have no 387, and the build
    now passes `-mno-80387` everywhere, so floating-point code would
    fail to link rather than trap at run time.
- **The font is ManiOS's own.** The classic VGA ROM font's provenance is
  murky, so the UI font is drawn fresh: 5x9 glyphs (7 rows above the
  baseline, 2 for descenders) in 6x11 cells, with a scale factor for
  titles. It is kept as ASCII art (`font.txt`), which anyone can edit
  and review. `tools/mkfont.py` checks it (every printable character,
  exactly once, well-formed rows) and generates `font_data.c`. The
  generated file is committed, so Python stays a test-only dependency,
  and `make test` fails if the two disagree.
- **libgfx lives in `desktop/libgfx/`.** It is the desktop's foundation,
  and it is linked into every program: a static archive only contributes
  what a program uses.

## Verification

- **At every boot:** `gtest` runs 27 pixel-exact checks of libgfx on
  in-memory canvases:
  - clipping at every edge and to a clip rectangle;
  - outlines;
  - Bresenham lines: one pixel per column, never more than half a
    pixel off the ideal line; lines entirely or partly off the canvas;
  - exact blending results (e.g. half-transparent red over blue is
    (128, 0, 127));
  - gradients and circles;
  - blits clipped at the destination, blending blits, and a blit moving
    rows down within one canvas, which must copy its rows bottom-up;
  - text: cell placement, scaling, the box for unknown characters,
    descenders, and every printable character having a glyph.

  The M11 marker reports it.
- **`tests/gfx_test.py`** (in `make test`) looks at the screen through
  QEMU's `screendump`:
  - For each mode (Bochs VBE 640x480 and 800x600, VGA mode 13h), it runs
    `gfxdemo`. It checks the image size, and the colour of each of the
    eight swatches and of their outline, where gfxdemo's layout puts
    them: exact for 32-bit colour, the quantised colour for VGA.
  - After each mode, a line of text echoed afterwards must be
    pixel-identical to the same line echoed before any graphics, which
    shows the font and palette are restored.
  - After the first mode, the line gfxdemo printed *while* graphics
    were up must be on the text screen.
  - `fbtest` exercises the device itself: its size, writing at an
    offset and reading it back, a write running off the end, end of
    file, `ENXIO` past the end and in text mode, `EINVAL` for a
    negative offset, and bad `fbctl` commands.
  - Unsupported modes are refused; `/dev/fb` can't be read in text mode.
  - The monitor's `pci` command lists the display adapter (console
    test).
- **Negative controls** (temporary sabotage, reverted afterward), each
  caught:

  | Sabotage | Caught by |
  |---|---|
  | Font not restored | The text-after-graphics comparisons, for every mode |
  | Text printed during graphics dropped | The shadow check |
  | Palette not restored exactly | The text comparisons |
  | `/dev/fb` writes not bounded | A kernel page fault in `fbtest`'s write past the end |
  | Dithering overshooting a quantisation step | The VGA swatch colours |
- **Not exercised by the tests:** a panic while graphics are on. No
  program can make the kernel panic, which is by design. The
  switch-to-text code runs on that path only.
- `make test`: 138 checks. The kernel still boots in 8 MiB (graphics
  cost nothing until a mode is set; a 640x480 canvas is 1.2 MB in the
  program).

## Known limits

- One program at a time should own the screen; nothing arbitrates yet.
  The desktop (M12) will.
- Writes go through system calls, a row at a time. Mapping the
  framebuffer into a program would save a copy, but isn't done.
- There is no hardware acceleration, no page flipping, and no vertical
  sync.
- Only ASCII in the font.
