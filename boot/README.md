# boot/

The bootloader layer (architecture layer 1).

In the first milestones (M1+), ManiOS targets the Multiboot 1
specification and relies on GRUB / `qemu-system-i386 -kernel` to perform
real-mode setup and the transition into 32-bit protected mode — see
[ADR-0001](../docs/adr/0001-bootloader-strategy.md). This directory is
where a **native** ManiOS bootloader (stage1 MBR + stage2) will live once
that becomes its own milestone.

Empty until then.
