/* ManiDOS: a disk operating system for ManiOS -- a DOS-style command
 * interpreter with drive letters, the classic commands, batch files and
 * disk tools that read FAT volumes from the disk itself. ManiOS's own:
 * no MS-DOS (or other DOS) code. docs/dos.md describes it.
 *
 *   main.c      start-up, the command line: reading, expanding,
 *               pipelines and redirection, running programs
 *   drives.c    drive letters and DOS paths, wildcards, directories
 *   commands.c  the internal commands
 *   batch.c     batch files: GOTO, IF, FOR, CALL, SHIFT
 *   fat.c       FAT volumes read raw: label, serial, free space, CHKDSK
 */
#ifndef MANIDOS_H
#define MANIDOS_H

#include <manios.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOS_VERSION "1.0"
#define PATH_MAX_DOS 128  /* a DOS path, "\DOCS\README.TXT" */
#define LINE_MAX_DOS 256  /* a command line */
#define ARGS_MAX_DOS 32

/* --- drives.c --- */

struct drive {
	bool present;
	char root[64];          /* its ManiOS directory: "/boot", "/n/ata0p1", "/" */
	char device[32];        /* its block device, "/dev/ata0p1"; "" for none */
	const char *kind;       /* "boot disk", "FAT", "ManiOS" */
	char cwd[PATH_MAX_DOS]; /* its current directory: "\" or "\DOCS" */
};

extern struct drive drives[26];
extern int current;         /* the current drive: 0 is A: */

void drives_init(void);
/* The current drive from ManiOS's current directory, if it is on one. */
void drives_follow_cwd(void);

/* A DOS path -- "C:\DOCS\X.TXT", "DOCS\X.TXT", "..\X", "D:" -- made
 * absolute: the drive and "\DOCS\X.TXT" (uppercase, no "." or "..",
 * never above the root). 0, or -1 for a drive that isn't there. */
int dos_resolve(const char *in, int *drive, char *path);
/* Where that is in ManiOS: "/n/ata0p1/docs/x.txt". */
void dos_to_manios(int drive, const char *path, char *out, size_t size);
/* "C:\DOCS\X.TXT". */
void dos_display(int drive, const char *path, char *out, size_t size);
/* Both at once: resolve `in` and give its ManiOS path; -1 if the drive
 * isn't there. */
int dos_manios(const char *in, char *out, size_t size);

/* What is there: ZKT_TYPE_DIR, _FILE, _DEVICE or _PIPE (and its size),
 * or -1 for nothing. */
int dos_type(const char *manios_path, uint32_t *size);

/* DOS wildcards, * and ?, ignoring case. */
bool wild_match(const char *pattern, const char *name);
bool has_wild(const char *s);

/* Calls fn for each entry of a ManiOS directory (not "." or ".."):
 * 0, or -1 if it can't be read. */
typedef void (*entry_fn)(const struct zkt_dirent *e, void *arg);
int each_entry(const char *manios_dir, entry_fn fn, void *arg);

/* Splits "DIR\PAT" at its last separator: the directory part ("" for
 * none) and the name part. */
void split_last(const char *in, char *dir, size_t dsize, char *name, size_t nsize);

/* Uppercase in place; returns s. */
char *upper(char *s);

/* --- fat.c --- */

struct fat_volume {
	int bits;                  /* 12 or 16 */
	uint32_t sector_size, cluster_size;
	uint32_t clusters, free_clusters, bad_clusters;
	uint32_t serial;
	bool has_serial;
	char label[12];            /* "" for none */
	/* private */
	uint32_t reserved, fats, fat_sectors, root_entries, root_start, data_start;
};

/* Reads a FAT volume's boot sector and its first FAT. 0, or -1 if the
 * device isn't a FAT12/16 volume. */
int fat_open(const char *device, struct fat_volume *v);
/* CHKDSK: the volume on drive `letter` checked, and the report
 * printed (with /V, every file's name too). Returns the number of
 * problems found, or -1 if it isn't a FAT12/16 volume. */
int fat_check(const char *device, char letter, bool verbose);

/* --- commands.c --- */

/* The raw text after an internal command's name. */
extern const char *cmd_rest;

struct command {
	const char *name;
	int (*run)(int argc, char **argv);
	const char *help;
};
extern const struct command COMMANDS[];
const struct command *find_command(const char *name);

/* Terminal size: asked of the terminal once (ESC [ 18 t), 80x24 if it
 * doesn't answer. */
int screen_rows(void);
int screen_cols(void);
/* Waits for Enter on the keyboard (the standard input ManiDOS started
 * with, even when a command's is redirected). */
void wait_enter(void);
/* The keyboard and the screen: the standard input and output ManiDOS
 * started with, whatever a command's are redirected to. */
extern int keyboard_fd, terminal_fd;

/* Prints a number with thousands separators, right-aligned in width. */
void print_count(unsigned long long n, int width);

/* The C library's line reading over a descriptor (fd 0 may be a pipe or
 * a file for a command in a pipeline). */
struct line_reader {
	int fd;
	char buf[1024];
	int len, pos;
	bool eof;
};
void reader_init(struct line_reader *r, int fd);
/* The next line without its newline, NULL at the end. */
char *reader_line(struct line_reader *r, char *out, size_t size);

/* --- batch.c --- */

int run_batch(const char *manios_path, const char *display, int argc, char **argv, bool call);
/* Expands %N and %%V inside a running batch (%VAR% always): into out. */
void expand(const char *in, char *out, size_t size);
bool in_batch(void);
void batch_goto(const char *label);
void batch_shift(void);
/* IF and FOR: the rest of the command line after the keyword. */
int if_command(const char *rest);
int for_command(const char *rest);
/* CALL: a batch file run as a subroutine (or any command). */
int run_call(const char *line);
/* Whether the batch file about to run was CALLed (and forget it). */
bool batch_call_pending(void);

/* --- main.c --- */

extern int errorlevel;
extern bool echo_on;
extern bool leaving;         /* EXIT was typed */

/* Runs one command line (pipelines, redirection): its errorlevel. */
int run_line(const char *line);
/* The environment: SET's variables. */
const char *env_get(const char *name);
int env_set(const char *name, const char *value); /* value NULL: remove */
void env_list(void);
void print_prompt(void);

#endif
