#ifndef ZKT_DRIVERS_FB_H
#define ZKT_DRIVERS_FB_H

/* Detects a Bochs VBE adapter (after pci_init) and registers devices
 * "fb" and "fbctl" (fb.c describes them). Text mode until asked. */
void fb_init(void);

/* Back to text mode without locking, so a panic message is visible. */
void fb_emergency_text(void);

#endif
