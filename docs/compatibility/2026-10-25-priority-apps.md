ManiOS priority application compatibility tests — 2026-10-25

Build: ManiOS 0.26.1-10252026, QEMU i386/486, 64 MiB RAM, serial console,
disposable FAT16 hard disk. Programs were launched inside ManiOS with `dosrun`.

| Target | Priority | Acquisition and test result |
| --- | --- | --- |
| VDE 1.97 | High | Downloaded [`VDE197.ZIP`](https://archive.org/download/vde-197/VDE197.ZIP), SHA-256 `e5f7d6f2ab03e71fbeeb3b726310aede17e1b1557a88873b60018027726e2f4e`. `dosrun C:/VDE.EXE` prints `Insufficient memory!` and returns to the shell. |
| VDE 1.65a | High | Downloaded [`VDE165A.ZIP`](https://archive.org/download/VDE165A_ZIP/VDE165A.ZIP), SHA-256 `d596c576b1ea44e89bdf5279c8c9adb7a599868453563e541d6071830faafc59`. `dosrun C:/VDE.COM` prints `Insufficient memory!` and returns to the shell. |
| Vern Buerg LIST 7.1a | High | Downloaded [`LIST.COM`](https://raw.githubusercontent.com/sebras/buerg/master/list/7.1a/LIST.COM), SHA-256 `aa954f7c51386ed66b88f6beb2a135330a68a0a5b2e104ea489e48288474e60b`. `dosrun C:/LIST.COM C:/INPUT.TXT` stops with `can't run AAM (at 1000:35F7)`. |
| PC-TYPE v1 DS | Medium | Downloaded [`PC-Type.zip`](https://archive.org/download/PC-Type_zip/PC-Type.zip), SHA-256 `2c3f7b837a3b100138cad08504f858ad5f66f58cd0da2379ff90c70e14e0658e`. `dosrun C:/PCTYPE.EXE` enters a blank new document. After Enter and attempted `hello`, screen output showed `qq`; no valid saved file or reopen check was confirmed. Archive documentation identifies PC-TYPE as a word processor evaluation release, not a typing tutor. |
| MicroEMACS 3.9 | High | Not run. The 1987 release notes identify the DOS executable package as `EMACSEXE.ARC` and its IBM executable as `MEIBM.EXE`, but that exact package was not obtained from the checked mirrors. No later MicroEMACS version was substituted. |
| PC-Outline 3.34 | Medium | Not run. Historical catalogs identify PC-SIG disk 480 and PC-Outline 3.34, but a disk image or executable was not obtained from the checked mirrors. |
| PC-DeskTeam | Medium | Not run. PC-SIG catalog metadata identifies disk 405 and `DESKTEAM.COM`; period descriptions identify a Sidekick-like organizer. PC-SIG lists disk 405, but it was not present on the checked 13th-edition CD sample, and no executable was obtained from the checked mirrors. Version therefore remains unverified. |

## Source notes

- VDE 1.97 archive: [Internet Archive item](https://archive.org/details/vde-197); the author announced 1.97 at the [VDE editor group](https://groups.google.com/g/vde_editor/c/TP8O2Bwe85A/m/6jmX45BLAQAJ).
- LIST source archive: [sebras/buerg on GitHub](https://github.com/sebras/buerg).
- MicroEMACS: [3.9 release notes](https://github.com/torvalds/uemacs/blob/master/readme.39e).
- PC-Outline 3.34 and PC-DeskTeam historical catalog descriptions: [Compute! 1989 catalog](https://colorcomputerarchive.com/coco/Documents/Magazines/Compute/Compute_Issue_114_1989_Nov.pdf) and [Computer History Museum archive](https://archive.computerhistory.org/resources/access/text/2023/11/102803715-05-010-acc.pdf).
- PC-SIG disk catalog and collection status: [PCjs collection](https://www.pcjs.org/software/pcx86/sw/misc/pcsig/0001-0999/) (disk 405 is listed; PC-Outline disk 480 is marked withdrawn).
- PC-DeskTeam disk number and contents: [PCjr newsletter](https://vtda.org/pubs/EugenePCjrClub/EugenePCjrClubNews_198910_Vol5No10.pdf).
- PC-TYPE archive: [Internet Archive item](https://archive.org/details/PC-Type_zip).

No keyboard editing, save, close, reopen or on-disk byte comparison was claimed for
VDE or LIST because their launch failures prevented reaching an editor/viewer. The
three unavailable packages remain explicitly untested; archive-search failure is
not a ManiOS compatibility result.
