/* install [-y] DISK [KEY=VALUE...] -- install ManiOS on a hard disk
 * (M14, ADR-0006).
 *
 * Copies the boot area this system was started from (/dev/bootarea:
 * the ManiOS boot loader and kernel) to DISK (ata0, ata1, ...) and makes
 * the disk boot it: an MBR with the ManiOS boot code, and one partition
 * of type 0xDA holding the boot area, at 1 MiB. KEY=VALUE words become
 * the installed system's kernel command line (sysname=, ip=, key=, rc=,
 * export=, ...); the boot loader lets you change it at each boot.
 *
 * Everything on DISK is lost: install asks you to type "yes" first,
 * unless -y. It reads everything back to check it. */
#include <bootarea.h>
#include <errno.h>
#include <manios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512
#define START_SECTOR 2048          /* the boot partition begins at 1 MiB */
#define ALIGN_SECTORS 2048         /* and ends on a MiB boundary */
#define CHUNK (64 * 1024)

static int fail(const char *what, const char *detail)
{
	fprintf(stderr, "install: %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
	return 1;
}

static int read_all(int fd, uint8_t *buf, uint32_t len)
{
	uint32_t done = 0;
	while (done < len) {
		long n = read(fd, buf + done, len - done > CHUNK ? CHUNK : len - done);
		if (n <= 0) {
			return -1;
		}
		done += (uint32_t)n;
	}
	return 0;
}

static int write_at(int fd, uint32_t offset, const uint8_t *buf, uint32_t len)
{
	if (lseek(fd, (long)offset, SEEK_SET) != (long)offset) {
		return -1;
	}
	uint32_t done = 0;
	while (done < len) {
		long n = write(fd, buf + done, len - done > CHUNK ? CHUNK : len - done);
		if (n <= 0) {
			return -1;
		}
		done += (uint32_t)n;
	}
	return 0;
}

/* Reads back len bytes at offset and compares them with buf. */
static int same_at(int fd, uint32_t offset, const uint8_t *buf, uint32_t len)
{
	static uint8_t back[CHUNK];
	if (lseek(fd, (long)offset, SEEK_SET) != (long)offset) {
		return 0;
	}
	for (uint32_t done = 0; done < len;) {
		uint32_t n = len - done > CHUNK ? CHUNK : len - done;
		if (read_all(fd, back, n) < 0 || memcmp(back, buf + done, n)) {
			return 0;
		}
		done += n;
	}
	return 1;
}

static int confirm(void)
{
	char line[16];
	printf("Type yes to go on: ");
	fflush(stdout);
	if (!fgets(line, sizeof(line), stdin)) {
		return 0;
	}
	line[strcspn(line, "\n")] = '\0';
	return !strcmp(line, "yes");
}

int main(int argc, char **argv)
{
	int yes = 0, a = 1;
	if (a < argc && !strcmp(argv[a], "-y")) {
		yes = 1;
		a++;
	}
	if (a >= argc) {
		fprintf(stderr, "usage: install [-y] DISK [KEY=VALUE...]\n"
		                "  DISK: a whole disk, such as ata0 (see ls /dev)\n");
		return 2;
	}
	const char *disk = argv[a++];
	if (!strncmp(disk, "/dev/", 5)) {
		disk += 5;
	}
	char cmdline[BA_CMDLINE_MAX] = "";
	for (; a < argc; a++) {
		if (!strchr(argv[a], '=')) {
			return fail("not KEY=VALUE", argv[a]);
		}
		if (strlen(cmdline) + strlen(argv[a]) + 2 > sizeof(cmdline)) {
			return fail("the command line is too long", 0);
		}
		if (cmdline[0]) {
			strcat(cmdline, " ");
		}
		strcat(cmdline, argv[a]);
	}

	/* What to install: the boot area this system came from. */
	int src = open("/dev/bootarea", OREAD);
	struct zkt_dirent st;
	if (src < 0 || fstat(src, &st) < 0) {
		return fail("there is no /dev/bootarea: this system was not started by the ManiOS "
		            "boot loader (from a ManiOS CD or disk), so there is nothing to install",
		            0);
	}
	uint32_t total = st.size;
	uint8_t *area = malloc(total);
	if (!area || read_all(src, area, total) < 0) {
		return fail("cannot read /dev/bootarea", strerror(errno));
	}
	close(src);
	if (total < BOOTAREA_ALIGN || memcmp(area, BOOTAREA_MAGIC, 8)
	    || bootarea_u32(area, BA_VERSION) != BOOTAREA_VERSION
	    || bootarea_u32(area, BA_TOTAL) != total
	    || bootarea_crc32(0, area + BOOTAREA_ALIGN, total - BOOTAREA_ALIGN)
	           != bootarea_u32(area, BA_CRC32)) {
		return fail("/dev/bootarea is damaged", 0);
	}
	uint32_t mbr_off = bootarea_u32(area, BA_MBR_OFF);
	if (bootarea_u32(area, BA_MBR_LEN) != 446 || mbr_off > total - 446) {
		return fail("/dev/bootarea has no MBR boot code", 0);
	}
	char version[17] = "";
	memcpy(version, area + BA_OS_VERSION, 16);

	/* Where: a whole disk, big enough. */
	if (!strncmp(disk, "ata", 3) && strchr(disk, 'p')) {
		return fail("install on a whole disk (such as ata0), not a partition", disk);
	}
	char path[32];
	snprintf(path, sizeof(path), "/dev/%s", disk);
	int dst = open(path, ORDWR);
	if (dst < 0 || fstat(dst, &st) < 0) {
		return fail(path, strerror(errno));
	}
	if (st.type != ZKT_TYPE_DEVICE) {
		return fail("not a disk", path);
	}
	uint32_t disk_sectors = st.size / SECTOR;
	uint32_t area_sectors = (total + SECTOR - 1) / SECTOR;
	uint32_t part_sectors = (area_sectors + ALIGN_SECTORS - 1) / ALIGN_SECTORS * ALIGN_SECTORS;
	if (disk_sectors < START_SECTOR + part_sectors) {
		char need[48];
		snprintf(need, sizeof(need), "it needs %lu KiB",
		         (unsigned long)(START_SECTOR + part_sectors) / 2);
		return fail("the disk is too small", need);
	}

	printf("ManiOS %s will be installed on %s (%lu MiB).\n"
	       "EVERYTHING ON %s WILL BE LOST.\n"
	       "The installed system's command line: \"%s\"\n",
	       version, disk, (unsigned long)(disk_sectors / 2048), disk, cmdline);
	if (!yes && !confirm()) {
		printf("Nothing was written.\n");
		return 1;
	}

	/* The boot area first, the MBR last: a disk left half-written
	 * doesn't look bootable. */
	memset(area + BA_CMDLINE, 0, BA_CMDLINE_MAX);
	memcpy(area + BA_CMDLINE, cmdline, strlen(cmdline));
	uint8_t mbr[SECTOR];
	memset(mbr, 0, sizeof(mbr));
	memcpy(mbr, area + mbr_off, 446);
	uint8_t *entry = mbr + 446;
	entry[0] = 0x80; /* active */
	entry[1] = 0xFE, entry[2] = 0xFF, entry[3] = 0xFF; /* CHS: "use the LBA" */
	entry[4] = BOOTAREA_PARTITION_TYPE;
	entry[5] = 0xFE, entry[6] = 0xFF, entry[7] = 0xFF;
	for (int i = 0; i < 4; i++) {
		entry[8 + i] = (uint8_t)(START_SECTOR >> (8 * i));
		entry[12 + i] = (uint8_t)(part_sectors >> (8 * i));
	}
	mbr[510] = 0x55;
	mbr[511] = 0xAA;

	printf("writing the boot area (%lu KiB)...\n", (unsigned long)(total / 1024));
	if (write_at(dst, START_SECTOR * SECTOR, area, total) < 0
	    || write_at(dst, 0, mbr, sizeof(mbr)) < 0) {
		return fail("writing the disk failed", strerror(errno));
	}
	printf("checking what was written...\n");
	if (!same_at(dst, START_SECTOR * SECTOR, area, total) || !same_at(dst, 0, mbr, sizeof(mbr))) {
		return fail("the disk doesn't read back what was written", 0);
	}
	close(dst);
	printf("ManiOS %s is installed on %s. Remove the CD and restart the machine.\n", version,
	       disk);
	return 0;
}
