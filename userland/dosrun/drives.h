/* The drives dosrun gives a DOS program: a DOS path (C:\DIR\FILE.EXT)
 * mapped onto a ManiOS path, so the program reads and writes the files
 * ManiDOS sees. The mapping is the same one ManiDOS uses (drives.c in
 * userland/dos/): a drive letter names a ManiOS directory, and the
 * path's backslashes become slashes.
 */
#ifndef DOSRUN_DRIVES_H
#define DOSRUN_DRIVES_H

#include <stddef.h>

/* Maps the DOS path in `path` (in place) to a ManiOS path. Returns 0,
 * or -1 if the drive isn't mounted or the path is too long for `n`. */
int dos_path(char *path, size_t n);

/* Sets where a drive letter points (from the command line, or a
 * default). `letter` is 'C', 'D', ...; `dir` is a ManiOS path. */
void dos_mount(char letter, const char *dir);

/* Mounts the drives ManiDOS mounts: A: the boot disk, Z: ManiOS, and
 * C: onwards the disks under /n. */
void dos_mount_defaults(void);

/* The current default drive (0 = A:, 2 = C:, ...), and selecting it.
 * dos_set_drive returns -1 if the drive isn't mounted. */
int dos_get_drive(void);
int dos_set_drive(int drive);

/* DOS keeps a separate current directory for each mounted drive. */
int dos_chdir(const char *path);
int dos_getcwd(int drive, char *out, size_t n);

#endif
