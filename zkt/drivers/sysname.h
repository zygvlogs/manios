#ifndef ZKT_DRIVERS_SYSNAME_H
#define ZKT_DRIVERS_SYSNAME_H

/* Registers device "sysname": this machine's name, from sysname= on the
 * command line ("manios" without it), as in Plan 9's /dev/sysname. A
 * node's name tells machines of a cluster apart (M13). */
void sysname_register(void);

#endif
