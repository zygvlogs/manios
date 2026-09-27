#ifndef ZKT_DRIVERS_ACPI_H
#define ZKT_DRIVERS_ACPI_H

/* Reads the ACPI tables for turning the machine off and resetting it,
 * and registers device "power" ("off", "reboot"). */
void acpi_init(void);

#endif
