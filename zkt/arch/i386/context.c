#include "context.h"

uintptr_t arch_context_init(uintptr_t stack_top, void (*entry)(void))
{
	uint32_t *sp = (uint32_t *)stack_top;

	*--sp = 0;                 /* return address for entry(): it never returns */
	*--sp = (uint32_t)entry;   /* where arch_context_switch's ret lands */
	*--sp = 0;                 /* ebp: ends the frame-pointer chain */
	*--sp = 0;                 /* ebx */
	*--sp = 0;                 /* esi */
	*--sp = 0;                 /* edi */
	return (uintptr_t)sp;
}
