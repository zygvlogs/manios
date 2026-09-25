/* Process entry. The kernel leaves ESP pointing at argc, then argv
 * (zkt_abi.h) -- a cdecl frame without a return address -- so _start
 * calls main(argc, argv) and hands its result to exit(). */
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
