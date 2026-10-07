/* The DOS and BIOS services dosrun gives a program: INT 21h (DOS), INT
 * 10h (video), INT 16h (keyboard), INT 1Ah (the clock). The CPU hands
 * an interrupt to dos_int(); it runs the service and sets the registers
 * the caller expects. Anything not implemented is refused the way DOS
 * refuses it (CF set, AX = an error), so a program that probes a
 * service gets a sensible answer instead of a wrong one.
 */
#ifndef DOSRUN_DOSAPI_H
#define DOSRUN_DOSAPI_H

#include "cpu.h"

struct dos_state;

struct dos_state *dos_new(struct cpu *c);
void dos_free(struct dos_state *d);

/* Runs the service for `vector`. Returns CPU_OK, or CPU_EXIT when the
 * program asked to end (INT 21h AH=4Ch): the exit code is in cpu->exit_code. */
enum cpu_stop dos_int(struct dos_state *d, struct cpu *c, uint8_t vector);

/* The DOS environment's memory: the PSP and the program's segment. */
void dos_set_program(struct dos_state *d, uint16_t psp_seg, uint16_t prog_seg);

/* The command tail (argv after the program name), as DOS's PSP holds it. */
void dos_set_tail(struct dos_state *d, const char *tail);

/* Builds the PSP at the segment dos_set_program() was given. */
void dos_build_psp(struct dos_state *d);

/* The file services (INT 21h AH=3C..43, 4E, 4F), in dosapi.c. */
enum cpu_stop dos_file(struct dos_state *d, struct cpu *c, uint8_t ah);

/* The video: mode 13h on ManiOS's screen, and copying the program's
 * framebuffer to it. */
void dos_video_mode13(struct dos_state *d);
void dos_video_text(struct dos_state *d);
void dos_video_present(struct dos_state *d);

#endif
