/* zlib's types only. ManiOS has no zlib; programs that can do without it
 * (OpenBSD's grep, built with NOZ: no -Z) still name its types. */
#ifndef MANIOS_ZLIB_H
#define MANIOS_ZLIB_H

typedef struct gzFile_s *gzFile;
typedef long z_off_t;

#endif
