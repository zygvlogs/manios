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
/* Transfers ownership of a raw keyboard descriptor to the DOS console. */
void dos_set_keyboard_fd(struct dos_state *d, int fd);

/* Runs the service for `vector` after cpu_enter_interrupt built its frame.
 * Saves returned status flags in that frame for the caller's IRET.
 * CPU_GUEST leaves the frame intact: execute the resolved IVT handler.
 * Returns CPU_OK with an IRET frame for the caller (or a newly EXECed
 * child). A child termination restores its parent frame. CPU_EXIT means
 * the top-level program ended; its exit code is in cpu->exit_code. */
enum cpu_stop dos_int(struct dos_state *d, struct cpu *c, uint8_t vector);

/* The DOS environment's memory: the PSP and the program's segment. */
void dos_set_program(struct dos_state *d, uint16_t psp_seg, uint16_t prog_seg);

/* The command tail (argv after the program name), as DOS's PSP holds it. */
void dos_set_tail(struct dos_state *d, const char *tail);

/* Builds the PSP at the segment dos_set_program() was given. */
void dos_build_psp(struct dos_state *d);
int dos_set_executable(struct dos_state *d, const char *host_path);
void dos_set_env(const char *name, const char *value);

/* The --env extras' limits: 16 variables, names and values sized for the
 * 0x100-byte environment block below the program's segment. */
#define ENV_EXTRA_MAX 16
#define ENV_NAME_MAX 32
#define ENV_VALUE_MAX 96
#define ENV_EXTRA_LIMIT 0xC0

/* Feed PIT0 from elapsed wall time. BIOS ticks advance when INT 08h runs. */
void dos_timer_advance(struct dos_state *d, uint64_t oscillator_clocks);
void dos_clock_advance(struct dos_state *d, uint32_t elapsed_ms);
/* Fetch queued host keys for DOS programs that poll the BIOS data area. */
void dos_keyboard_poll(struct dos_state *d);

/* Logical PC-speaker tone (0 when gated/disconnected). No audio backend. */
unsigned dos_speaker_frequency(struct dos_state *d);

/* The file services (INT 21h AH=3C..43, 4E, 4F), in dosapi.c. */
enum cpu_stop dos_file(struct dos_state *d, struct cpu *c, uint8_t ah);

/* The video: mode 13h on ManiOS's screen, and copying the program's
 * framebuffer to it. */
void dos_video_mode13(struct dos_state *d);
void dos_video_text(struct dos_state *d);
void dos_video_present(struct dos_state *d);

#endif
