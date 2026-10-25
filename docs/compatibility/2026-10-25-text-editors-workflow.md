Text editor save/reopen workflow — 2026-10-25

ManiOS 0.26.1-10252026; QEMU i386, CPU 486, 64 MiB RAM; disposable FAT16
hard-disk images. Each program edited `C:/INPUT.TXT`, initially containing
`first line\r\nsecond line\r\n` (25 bytes). The operation inserted `EDITED ` at
the start of the first line. After closing the editor, `INPUT.TXT` was copied
from the FAT16 image with mtools and compared byte-for-byte with the expected
32-byte value `EDITED first line\r\nsecond line\r\n`.

| Step | QEdit Advanced 2.15 | TED 3.0 | FreeDOS EDLIN 2.24 |
| --- | --- | --- | --- |
| Open existing file | Pass; both original lines displayed. | Pass; both original lines displayed. | Pass; reported `2 lines read`. |
| Enter text | Pass; typed `EDITED `. | Pass; typed `EDITED `. | Fail; neither emulated keyboard input nor serial line input was accepted at the `*` prompt. |
| Edit existing text | Pass; prefix inserted at file start. | Pass; prefix inserted at file start. | Not reached. |
| Save | Pass; Ctrl+K, S. | Pass; F7 save-and-exit, confirmed with Enter. | Not reached. |
| Close | Pass; Alt+X returned to the ManiOS shell. | Pass; F7 returned to the ManiOS shell. | Not verified; the process had to be stopped after input attempts were ignored. |
| Reopen | Pass; a fresh launch displayed the edited content. | Pass; a fresh launch displayed the edited content. | Not tested because no edit/save was possible. |
| FAT16 bytes | Pass; exact expected bytes. | Pass; exact expected bytes. | Unchanged from original. |

Hashes of the extracted FAT16 file contents:

```text
original (QEdit, TED, EDLIN): a6ad0f6d0647ff79b6c9fbce44e1f9955b395b563f661705a691949bf6e0a75e
saved (QEdit and TED):       2051ba08d3f1ec6d6b2fd21a5a9a23b8d003353ffd5214194d948a417f3f879f
```

Reopen screenshots: [QEdit](2026-10-25-qedit-reopened.png),
[TED](2026-10-25-ted-reopened.png). EDLIN's initial two-line read is shown in
[its screenshot](2026-10-25-edlin.png). EDLIN commands attempted after launch
were `1`, `EDITED first line`, and `e`; the file remained byte-identical.
