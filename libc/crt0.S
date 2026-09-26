/* Process entry. The kernel leaves ESP pointing at argc, then argv
 * (zkt_abi.h) -- a cdecl frame without a return address -- so _start
 * calls __libc_init(argc, argv), then main(argc, argv), and hands its
 * result to exit(). */
#include <zkt_abi.h>

#ifndef ZKT_NOTE_VERSION
#define ZKT_NOTE_VERSION ZKT_ABI_VERSION
#endif

.section .text
.global _start
.type _start, @function
_start:
	xor %ebp, %ebp          /* marks the outermost frame for debuggers */
	pop %esi                /* argc (esi and edi survive the calls) */
	pop %edi                /* argv */
	and $-16, %esp
	sub $8, %esp            /* 16-byte aligned at the call, after two pushes */
	push %edi
	push %esi
	call __libc_init        /* the program's name, from argv[0] (progname.c) */
	add $8, %esp
	push %edi
	push %esi
	call main
	add $16, %esp
	push %eax
	call exit
.size _start, . - _start

/* The ABI version this program was built for; the kernel refuses to run
 * a program without it (zkt_abi.h). The current one, unless the build
 * asks for an older one (a test of the kernel's compatibility). Every program links crt0.o, so every
 * program carries it; userland/user.ld puts it in a PT_NOTE segment. */
.section .note.zkt.abi, "a", @note
.balign 4
.long 2f - 1f            /* name size, NUL included */
.long 4f - 3f            /* descriptor size */
.long ZKT_NOTE_ABI
1:	.asciz ZKT_NOTE_NAME
2:	.balign 4
3:	.long ZKT_NOTE_VERSION
4:
