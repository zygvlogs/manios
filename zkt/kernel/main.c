#include <stdint.h>
#include "arch.h"
#include "bootmod.h"
#include "cmdline.h"
#include "cpu.h"
#include "device.h"
#include "drivers.h"
#include "fs_init.h"
#include "heap.h"
#include "kconsole.h"
#include "kerrno.h"
#include "kprintf.h"
#include "mm_selftest.h"
#include "monitor.h"
#include "multiboot.h"
#include "net.h"
#include "net_selftest.h"
#include "zrp.h"
#include "panic.h"
#include "pmm.h"
#include "sched.h"
#include "sched_selftest.h"
#include "sha256.h"
#include "kstring.h"
#include "timer.h"
#include "user_selftest.h"
#include "vfs_selftest.h"
#include "vmm.h"
#include "zkt_abi.h"

#define MAX_MEM_REGIONS 64

/* export=PATH serves PATH (in the kernel namespace) over ZRP. */
/* ZRP at boot (M13): the cluster key (key=), the server on ZRP_PORT,
 * and the default export (export=). */
static void start_zrp(void)
{
	char text[VFS_PATH_MAX + 1];
	if (cmdline_get("key", text, sizeof(text))) {
		uint8_t key[ZRP_KEY];
		zrp_key_derive(text, key);
		zrp_key_set(key);
		memset(key, 0, sizeof(key));
		memset(text, 0, sizeof(text));
		kprintf("zrp: cluster key set: sessions are authenticated\n");
	}
	zrp_start_main();
	struct zrp_server *srv = zrp_main_server();
	if (srv && cmdline_get("export", text, sizeof(text))) {
		int rc = zrp_export(srv, "", text, 0);
		if (rc == 0) {
			kprintf("zrp: exporting %s on udp port %d\n", text, ZRP_PORT);
		} else {
			kprintf("zrp: cannot export %s: %s\n", text, kstrerror(rc));
		}
	}
}

/* verbose=1 on the command line: the whole boot log on the screen too.
 * Otherwise the screen shows what is being checked, and the log goes
 * only to COM1 (where the tests read it). */
static bool verbose_boot(void)
{
	char value[8];
	return cmdline_get("verbose", value, sizeof(value)) && strcmp(value, "0") != 0;
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_phys)
{
	arch_early_init();
	kconsole_init();

	kconsole_write("ManiOS " MANIOS_VERSION " / ZKT (ZygKernel Technology)\n");

	if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		panic("not booted by a Multiboot loader; no memory map available");
	}

	char line[CMDLINE_MAX];
	multiboot_cmdline(multiboot_info_phys, line, sizeof(line));
	cmdline_set(line);
	if (!verbose_boot()) {
		kconsole_set_quiet(true);
		kconsole_progress("Self-tests:");
	}
	kconsole_write("Milestone M1: kernel console reached.\n");

	struct mem_region regions[MAX_MEM_REGIONS + BOOT_MODULES_MAX];
	size_t region_count = multiboot_memory_regions(multiboot_info_phys, regions,
	                                               MAX_MEM_REGIONS);
	/* Modules stay where the loader put them: their frames are reserved. */
	struct boot_module modules[BOOT_MODULES_MAX];
	size_t module_count = multiboot_modules(multiboot_info_phys, modules, BOOT_MODULES_MAX);
	for (size_t i = 0; i < module_count; i++) {
		regions[region_count++] = (struct mem_region){
			modules[i].start, modules[i].end - modules[i].start, false
		};
	}
	pmm_init(regions, region_count);
	vmm_init();
	heap_init();
	bootmod_init(modules, module_count);

	kconsole_write("memory: ");
	kconsole_write_dec(pmm_usable_frames() * 4);
	kconsole_write(" KiB usable, ");
	kconsole_write_dec(pmm_free_frames());
	kconsole_write(" frames free\n");

	mm_selftest();
	kconsole_write("Milestone M2: memory manager online (self-test passed).\n");
	kconsole_progress(" memory");

	sched_init(); /* from here on, this is thread "main" */
	timer_init();
	cpu_enable_interrupts();
	/* Returns only after several ticks, i.e. only if IRQs are delivered
	 * and acknowledged (the PIC sends nothing more until EOI). */
	timer_sleep_ms(50);
	kconsole_write("Milestone M3: interrupts online (PIT timer at ");
	kconsole_write_dec(TIMER_HZ);
	kconsole_write(" Hz).\n");
	kconsole_progress(", interrupts");

	sched_selftest_cooperative();
	sched_enable_preemption();
	sched_selftest_preemptive();
	kconsole_write("Milestone M4: kernel threads online "
	               "(cooperative + preemptive scheduling, self-test passed).\n");
	kconsole_progress(", threads");

	sched_selftest_sync();
	drivers_init();
	kprintf("Milestone M5: driver framework online "
	        "(keyboard + serial console input, mutex self-test passed).\n");
	kprintf("Milestone M6: ATA storage driver online.\n");
	kconsole_progress(", devices, disks");
	kprintf("devices:");
	for (struct device *d = device_next(0); d; d = device_next(d)) {
		kprintf(" %s", d->name);
	}
	kprintf("\n");

	fs_init();
	vfs_selftest();
	kprintf("Milestone M7: VFS online (namespaces, union directories, FAT; self-test passed).\n");
	kconsole_progress(", files");

	user_selftest();
	kprintf("Milestone M8: userspace online (ring 3 processes, system calls; self-test passed).\n");
	kconsole_progress(", programs");

	libc_selftest();
	kprintf("Milestone M9: libc, ABI v%d and shell online (self-test passed).\n", ZKT_ABI_VERSION);
	kconsole_progress(",\n            C library"); /* under "memory", not across the edge */

	if (!sha256_selftest()) {
		panic("selftest: SHA-256 or HMAC gave a wrong answer");
	}
	net_init();
	net_selftest();
	kprintf("Milestone M10: network online (IPv4/UDP, ZRP; loopback self-test passed).\n");
	kconsole_progress(", network");
	start_zrp();

	gfx_selftest();
	kprintf("Milestone M11: graphics online (framebuffer, 2D library; self-test passed).\n");
	kconsole_progress(", graphics");

	channel_selftest();
	kprintf("Milestone M12: pipes, input devices and userspace file servers online "
	        "(self-test passed).\n");
	kconsole_progress(", windows");

	cluster_selftest();
	kprintf("Milestone M13: cluster roles online (authenticated ZRP2, exports, cpu service; "
	        "self-test passed).\n");
	kconsole_progress(", cluster. All passed.\n");
	kconsole_set_quiet(false);
	net_start_dhcp(3000); /* after the self-tests: they count threads and memory */
	kprintf("ManiOS " MANIOS_VERSION " is ready. Try ls /bin (programs), help (the shell), fetch, manide (the desktop).\n");

	if (!thread_create("console", console_main, 0)) {
		panic("could not start the console thread");
	}
	thread_exit();
}
