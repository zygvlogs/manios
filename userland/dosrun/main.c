/* dosrun -- run a DOS program (.COM or .EXE) on ManiOS.
 *
 *   dosrun [--mount LETTER=/directory] [--env NAME=value] PROGRAM [ARGS...]
 *
 * The program is loaded into an 8086 interpreter (cpu.c), and its DOS
 * and BIOS calls (dosapi.c) are served by ManiOS: the console is this
 * program's terminal, and the files are ManiDOS's, through the same
 * drive letters (drives.c). docs/dosrun.md describes it.
 *
 * The interpreter is ManiOS's own, written from nothing. The MS-DOS 4.0
 * source (MIT) is a reference for the DOS functions' behavior, not code
 * copied from it.
 */
#include "cpu.h"
#include "dosapi.h"
#include "drives.h"
#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>

/* manios.h declares the native fstat ABI; this file uses POSIX stat. */
extern uint32_t uptime_ms(void);

/* How many instructions between checks of the clock and the keyboard,
 * so a program in a tight loop still sees input. */
#define SLICE 100000

int main(int argc, char **argv)
{
	char letters[26]; const char *roots[26]; int mounts = 0;
	char envnames[ENV_EXTRA_MAX][ENV_NAME_MAX], envvalues[ENV_EXTRA_MAX][ENV_VALUE_MAX];
	int envs = 0;
	while (argc > 1 && (!strcmp(argv[1], "--mount") || !strcmp(argv[1], "--env"))) {
		if (!strcmp(argv[1], "--mount")) {
			struct stat st;
			if (argc < 4 || mounts == 26 || !isalpha((unsigned char)argv[2][0]) ||
			    argv[2][1] != '=' || argv[2][2] != '/' || strlen(argv[2] + 2) >= 64 ||
			    stat(argv[2] + 2, &st) < 0 || !S_ISDIR(st.st_mode)) {
				fprintf(stderr, "dosrun: --mount requires LETTER=/absolute/directory followed by a program\n");
				return 2;
			}
			letters[mounts] = argv[2][0]; roots[mounts++] = argv[2] + 2;
		} else {
			char *eq = argc < 3 ? NULL : strchr(argv[2], '=');
			if (!eq || eq == argv[2] || eq - argv[2] >= ENV_NAME_MAX ||
			    strlen(eq + 1) >= ENV_VALUE_MAX || envs == ENV_EXTRA_MAX) {
				fprintf(stderr, "dosrun: --env requires NAME=value followed by a program\n");
				return 2;
			}
			for (int i = 0; argv[2][i] != '='; i++)
				envnames[envs][i] = toupper((unsigned char)argv[2][i]);
			snprintf(envvalues[envs], sizeof(envvalues[0]), "%s", eq + 1);
			envs++;
		}
		argc -= 2; argv += 2;
	}
	if (argc < 2) {
		fprintf(stderr, "usage: dosrun [--mount LETTER=/directory] [--env NAME=value] PROGRAM [ARGS...]\n");
		return 2;
	}

	struct cpu cpu;
	cpu_init(&cpu);
	if (!cpu.mem) {
		fprintf(stderr, "dosrun: out of memory\n");
		return 1;
	}

	/* The command tail: the arguments after the program's name. */
	char tail[128];
	tail[0] = '\0';
	for (int i = 2; i < argc; i++) {
		if (i > 2) {
			strncat(tail, " ", sizeof(tail) - strlen(tail) - 1);
		}
		strncat(tail, argv[i], sizeof(tail) - strlen(tail) - 1);
	}

	dos_mount_defaults();
	for (int i = 0; i < mounts; i++) dos_mount(letters[i], roots[i]);
	/* The program's path is a DOS path: map it onto ManiOS's, the way
	 * the program's own file calls will be. */
	char prog[256];
	snprintf(prog, sizeof(prog), "%s", argv[1]);
	if (dos_path(prog, sizeof(prog)) < 0) {
		fprintf(stderr, "dosrun: %s: no such drive\n", argv[1]);
		cpu_free(&cpu);
		return 1;
	}

	char err[256];
	if (load_program(&cpu, prog, tail, err, sizeof(err)) < 0) {
		fprintf(stderr, "dosrun: %s\n", err);
		cpu_free(&cpu);
		return 1;
	}

	struct dos_state *dos = dos_new(&cpu);
	if (!dos) {
		fprintf(stderr, "dosrun: out of memory\n");
		cpu_free(&cpu);
		return 1;
	}
	for (int i = 0; i < envs; i++) dos_set_env(envnames[i], envvalues[i]);
	dos_set_program(dos, 0x1000, 0x1010);
	dos_build_psp(dos);
	dos_set_tail(dos, tail);

	/* Run until the program ends, faults, or runs away. */
	int status = 0;
	unsigned long since_present = 0;
	unsigned since_clock = 0;
	uint32_t last_clock = uptime_ms();
	for (;;) {
		if (++since_clock >= 1000) {
			since_clock = 0;
			uint32_t now = uptime_ms();
			dos_clock_advance(dos, now - last_clock);
			last_clock = now;
		}
		/* Every so often, show a mode 13h program's framebuffer. */
		if (++since_present >= 20000) {
			since_present = 0;
			dos_keyboard_poll(dos);
			dos_video_present(dos);
		}
		enum cpu_stop stop = cpu_step(&cpu);
		switch (stop) {
		case CPU_OK:
			break;
		case CPU_INT: {
			/* Run an installed guest handler with its INT frame intact,
			 * or serve an unhooked host API and return through IRET. */
			enum cpu_stop r = dos_int(dos, &cpu, cpu.int_no);
			if (r == CPU_GUEST) break;
			if (r == CPU_EXIT) {
				status = cpu.exit_code;
				goto done;
			}
			cpu.ip = pop16(&cpu);
			cpu.sreg[1] = pop16(&cpu);
			cpu.flags = pop16(&cpu);
			break;
		}
		case CPU_IRET:
			/* cpu_step already restored the interrupt frame. */
			break;
		case CPU_EXIT:
			status = cpu.exit_code;
			goto done;
		case CPU_HALT:
			/* A program that halts has ended. */
			goto done;
		case CPU_FAULT:
			fprintf(stderr, "dosrun: %s: can't run %s (at %04X:%04X)\n",
			        argv[1], cpu_fault_name(cpu.fault_opcode),
			        cpu.sreg[1], cpu.ip);
			if ((cpu.fault_opcode >= 0xE4 && cpu.fault_opcode <= 0xE7) ||
			    (cpu.fault_opcode >= 0xEC && cpu.fault_opcode <= 0xEF)) {
				uint16_t port = cpu.fault_opcode < 0xEC ?
				                rd8(&cpu, cpu.sreg[1], cpu.ip) : cpu.r[DX];
				fprintf(stderr, "dosrun: unsupported %s port %04X, AL=%02X AX=%04X; bytes at %04X:%04X:",
				        cpu.fault_opcode & 2 ? "OUT" : "IN", port,
				        cpu.r[AX] & 255, cpu.r[AX], cpu.sreg[1], (uint16_t)(cpu.ip - 9));
				for (int j = -9; j < 7; j++)
					fprintf(stderr, " %02X", rd8(&cpu, cpu.sreg[1], (uint16_t)(cpu.ip + j)));
				fprintf(stderr, "\n");
			}
			status = 1;
			goto done;
		}
		if (cpu.insns > 2000000000u) {
			fprintf(stderr, "dosrun: %s: ran too long\n", argv[1]);
			status = 1;
			goto done;
		}
	}

done:
	dos_video_present(dos); /* flush direct text writes even for short programs */
	dos_video_text(dos); /* back to text, if a program left mode 13h on */
	dos_free(dos);
	cpu_free(&cpu);
	return status;
}
