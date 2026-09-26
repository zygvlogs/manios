/* Declarations shared with BSD sources (third_party/openbsd). */
#ifndef MANIOS_SYS_CDEFS_H
#define MANIOS_SYS_CDEFS_H

#define __dead __attribute__((__noreturn__))
#define __unused __attribute__((__unused__))
#define __BEGIN_DECLS
#define __END_DECLS
/* OpenBSD's compiler checks buffer bounds with __attribute__((__bounded__
 * (...))); GCC doesn't know the attribute, so it is dropped. */
#define __bounded__(...)

#endif
