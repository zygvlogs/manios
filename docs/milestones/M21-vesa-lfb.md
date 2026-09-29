# M21 — a real display adapter's own linear framebuffer (VESA BIOS Extensions)

**Status:** Achieved (2026-09-27). Released as **0.21.0**. Found within
hours of 0.20.0's real-hardware boot: `manide` ("cannot set a 800x600
mode: no such device (a Bochs VBE display is needed)") on a laptop with
an ATI graphics chip, none of the emulator-specific hardware
`zkt/drivers/fb.c` already drove. Asked for as "Yes VBE". `gfx=auto` on
the boot line now has ManiOS's own boot loader ask the display's own
VESA BIOS Extensions for a linear framebuffer before ManiOS starts, so
`manide` and `gfxdemo` can draw to a real adapter, not only an
emulator's.

## What M21 delivers

| Piece | Files | Summary |
|---|---|---|
| The probe | `boot/stage2_entry.S` (`vbe_probe`) | Real mode, before the switch to protected mode: VBE 2.0's INT 10h services find the biggest mode up to 1024x768 of the 32-bit XRGB kind fb.c already draws, with a linear framebuffer, and set it -- only with `gfx=auto` on the (by then final) command line |
| To the kernel | `boot/stage2.c`, `zkt/kernel/multiboot.h`/`.c` | The mode's address, size and format, as Multiboot 1's own framebuffer fields (flags bit 12); read once, at boot, and kept for `fb_init()` |
| In the driver | `zkt/drivers/fb.c` | A third kind of graphics mode, `LFB`, fixed at whatever size the loader found; used only where there is no Bochs VBE adapter, which can do more (any size, changed at will) |
| The message | `desktop/manide/main.c` | Mentions `gfx=auto` when there is truly no display ManiOS can drive |

## Design decisions

**Opt in, not automatic.** Setting a VESA mode can't be undone: once
real mode is behind, there is no way left to ask the BIOS for anything
else (no V86 monitor to call it through), so a boot loader that set one
by default would take away every machine's text console, unless and
until something asked for graphics -- on a text-only ManiOS server or a
cluster node, forever. `gfx=auto` is read from the finalised command
line (after the loader's own edit prompt), and only then does
`vbe_probe` touch the display BIOS at all; every other boot -- the vast
majority -- is exactly as before M21, VESA calls included, not merely
their effect.

**Only the shape fb.c already knows.** `zkt/drivers/fb.c`'s Bochs VBE
path draws 32-bit pixels, 0x00RRGGBB (XRGB8888); libgfx and the whole
desktop are built on that. `vbe_probe` only accepts VESA modes of
exactly that shape (BitsPerPixel 32, RedMaskSize/FieldPosition 8/16,
Green 8/8, Blue 8/0, one plane, the mode's linear-framebuffer bit set),
so the kernel needs nothing new to draw to one -- `fb.c`'s LFB kind is
a few dozen lines, not a second graphics driver. A card whose VESA BIOS
offers only 8- or 16-bit colour (many older ones; QEMU's Cirrus
emulation among them) has nothing `gfx=auto` can use, and says so.

**Fixed, not resizable.** A Bochs VBE mode can be changed at will, by
writing its registers again; a VESA mode set by the BIOS can't be, once
protected mode has no BIOS to call. So `set_lfb()` (fb.c) takes no
size: whatever `mode W H` or `mode vga` was written, an LFB machine
gets the one mode there is, and reads back its real size -- which is
exactly what `gfx_screen_open()` (`desktop/libgfx/screen.c`) already
does with any mode, so `manide` and `gfxdemo` need no change at all to
use it.

**The registers still win.** Where both exist -- every card this is
tested against, since QEMU's own emulators are also driven through a
real (SeaBIOS) VESA BIOS on top of the same Bochs registers -- `fb.c`
prefers the registers: they can do more, and are what the whole test
suite (`gfx_test.py`) already exercises. `vbe_probe` still runs and
still sets its own mode when asked (proving that path works even
there), but the kernel simply doesn't use it.

**Biggest, capped.** ManiOS draws everything in software (there is no
2D acceleration to ask a real card for), so an enormous mode costs
old, slow CPUs real time per frame for no benefit `gfx=auto`'s caller
asked for. `vbe_probe` takes the biggest matching mode up to 1024x768;
a card offering only bigger ones finds nothing usable, rather than
something ManiDE would be slow on.

## Bugs found on the way

- On the very first real-hardware boot (0.20.0, no `gfx=auto` yet:
  M21's actual origin), `manide` refused for exactly the reason
  `docs/install.md` already documented -- no Bochs VBE registers on
  real hardware. Not a bug in 0.20.0; the reason this milestone exists.
- The first version of `vbe_probe` ran before the loader's edit
  prompt, so a user-typed `gfx=auto` at that prompt would have been
  missed (only the header's own default command line would have been
  seen). Moved to run after it, reading the line as finally decided.

## Verification

- **`tests/vbe_test.py`** (new, in `make test`): five real CD boots
  (`-kernel` never reaches `stage2_entry.S`, so `-append` can't test
  this; each boot's command line is baked into its own small boot area
  and ISO, `tools/mkbootarea.py --cmdline`) --
  - no `gfx=`: no probe, the usual boot, unchanged;
  - `gfx=1024x768` (not `auto`, which is all M21 takes): also ignored;
  - `gfx=auto` with QEMU's own display (`-vga std`, Bochs VBE
    registers): a real mode still probed and set over genuine BIOS
    calls (SeaBIOS's own VBE, on the same hardware the registers also
    reach), but the registers still drive graphics -- `fb.c`'s LFB
    line is never printed;
  - `gfx=auto`, `-vga cirrus` (a different, real VESA BIOS with no
    32-bit linear-framebuffer mode): "no VESA linear framebuffer",
    boots as usual;
  - `gfx=auto`, `-vga qxl` (a different PCI device again, no Bochs
    registers, whose BIOS does answer with a usable 1024x768x32 mode):
    the kernel's LFB path, genuinely exercised -- `gfxdemo` draws to
    it and a `screendump` is read back pixel for pixel, as
    `gfx_test.py` already checks the Bochs VBE path.

  No sabotage was needed to reach the real LFB code path in an
  automated, always-reproducible way: QXL is real, unmodified QEMU
  hardware, with its own, different VESA BIOS.
- `make test`: 504 checks (499 at 0.20), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | `gfx=auto`'s "not requested" path shared the printing one | a plain boot printed a `graphics:` line |
  | The mode's pitch (BytesPerScanLine) read from the wrong field | garbled swatch colours (wrong row stride) (a first try, dropping the mode number's linear-framebuffer bit, showed nothing: QEMU's emulated adapters don't distinguish a linear framebuffer from the legacy bank-switched window the way real 1990s-era hardware did) |
  | The framebuffer never reaching the Multiboot information | the kernel not taking the loader's framebuffer |
  | The framebuffer's pixel-type check inverted (rejecting RGB) | likewise |
  | The Bochs VBE/loader precedence reversed | the loader's mode used instead of the registers |

## Known limits

- Set once, at boot: no changing the mode (size or leaving it) once
  ManiOS is running, and no second display adapter tried if the first
  gives nothing.
- 32-bit colour only (no 8-, 16- or 24-bit VESA modes); at most
  1024x768, even where a card's BIOS offers more.
- Leaving graphics (`manide`'s Alt+Q, or `gfxdemo`'s Enter) tries to
  restore text mode by writing VGA's own registers, exactly as the
  Bochs VBE path already does -- and on many real cards, whose VESA
  mode a plain VGA register poke can't undo, that fails: the screen
  stays in graphics, showing whatever was last drawn, though the
  machine (and its keyboard) keep working. There is no way to call the
  BIOS again to ask properly, without a V86 monitor ManiOS doesn't
  have yet.
- Real hardware other than the one M21 was written for hasn't been
  tried; the QEMU adapters above are what is tested.
