/* dosrun -- run a DOS program (.COM or .EXE) on ManiOS.
 *
 *   dosrun PROGRAM [ARGS...]
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

/* How many instructions between checks of the clock and the keyboard,
 * so a program in a tight loop still sees input. */
#define SLICE 100000

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: dosrun PROGRAM [ARGS...]\n");
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
	dos_set_program(dos, 0x1000, 0x1010);
	dos_build_psp(dos);
	dos_set_tail(dos, tail);

	/* Run until the program ends, faults, or runs away. */
	int status = 0;
	unsigned long since_present = 0;
	for (;;) {
		/* Every so often, show a mode 13h program's framebuffer. */
		if (++since_present >= 20000) {
			since_present = 0;
			dos_video_present(dos);
		}
		enum cpu_stop stop = cpu_step(&cpu);
		switch (stop) {
		case CPU_OK:
			break;
		case CPU_INT: {
			/* cpu_step pushed the frame and set CS:IP to the
			 * vector's handler (the IVT is empty, so that is
			 * 0000:0000). Run the service, then IRET back. */
			enum cpu_stop r = dos_int(dos, &cpu, cpu.int_no);
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
			cpu.ip = pop16(&cpu);
			cpu.sreg[1] = pop16(&cpu);
			cpu.flags = pop16(&cpu);
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
	dos_video_text(dos); /* back to text, if a program left mode 13h on */
	dos_free(dos);
	cpu_free(&cpu);
	return status;
}
