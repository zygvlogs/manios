/* From OpenBSD (third_party/openbsd/lib/libc/gen): the last part of a
 * path, and all but it. Both return a static buffer. */
#ifndef MANIOS_LIBGEN_H
#define MANIOS_LIBGEN_H

char *basename(char *path);
char *dirname(char *path);

#endif
