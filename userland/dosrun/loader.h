/* Loading a DOS program into the CPU's memory: a .COM (a bare image at
 * offset 0x100 of the program segment) or an .EXE (the MZ header, with
 * relocations). The header layout is the MS-DOS 4.0 source's EXE.INC
 * (MIT); the code here is ManiOS's own.
 */
#ifndef DOSRUN_LOADER_H
#define DOSRUN_LOADER_H

#include "cpu.h"

/* Loads the file at `path` into `c`'s memory, sets up the PSP and the
 * segment registers, and leaves CS:IP at the entry point. Returns 0, or
 * -1 with a message in `err`. */
int load_program(struct cpu *c, const char *path, const char *tail,
                 char *err, unsigned long errlen);

#endif
