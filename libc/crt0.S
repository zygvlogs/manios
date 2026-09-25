/* Process entry. The kernel leaves ESP pointing at argc, then argv
 * (zkt_abi.h) -- a cdecl frame without a return address -- so _start
 * calls main(argc, argv) and hands its result to exit(). */
#include <zkt_abi.h>

.section .text
.global _start
.type _start, @function
_start:
	xor %ebp, %ebp          /* marks the outermost frame for debuggers */
	pop %eax                /* argc */
	pop %ecx                /* argv */
	and $-16, %esp
	sub $8, %esp            /* 16-byte aligned at the call, after two pushes */
	push %ecx
	push %eax
	call main
	add $16, %esp
	push %eax
	call exit
.size _start, . - _start

/* The ABI version this program was built for; the kernel refuses to run
 * a program without it (zkt_abi.h). Every program links crt0.o, so every
 * program carries it; userland/user.ld puts it in a PT_NOTE segment. */
.section .note.zkt.abi, "a", @note
.balign 4
.long 2f - 1f            /* name size, NUL included */
.long 4f - 3f            /* descriptor size */
.long ZKT_NOTE_ABI
1:	.asciz ZKT_NOTE_NAME
2:	.balign 4
3:	.long ZKT_ABI_VERSION
4:
