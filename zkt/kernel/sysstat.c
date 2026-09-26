/* Devices "sysstat" and "ps" (sysstat.h). */
#include "sysstat.h"
#include "cpu.h"
#include "cpuinfo.h"
#include "device.h"
#include "kprintf.h"
#include "kstring.h"
#include "pmm.h"
#include "process.h"
#include "sched.h"
#include "timer.h"

#define PS_MAX 96          /* processes listed */
#define PS_LINE 96         /* "pid ppid state cpu mem name\n", at most */
#define TEXT_MAX (PS_MAX * PS_LINE)

/* Each device's text is made when it is read from offset 0 and kept for
 * the reads that follow, so a reader that takes it in pieces sees one
 * snapshot. Making and copying run with interrupts off: two readers
 * can't mix their snapshots. */
struct text {
	char buf[TEXT_MAX];
	size_t len;
};

static struct text sysstat_text, ps_text;
static struct process_info ps_info[PS_MAX];
static char cpu_name[64];

static long read_text(struct text *t, void (*make)(struct text *), uint32_t offset, void *buf,
                      size_t len)
{
	uint32_t flags = cpu_irq_save();
	if (offset == 0) {
		make(t);
	}
	long n = 0;
	if (offset < t->len) {
		n = (long)(len < t->len - offset ? len : t->len - offset);
		memcpy(buf, t->buf + offset, (size_t)n);
	}
	cpu_irq_restore(flags);
	return n;
}

/* ksnprintf returns the untruncated length. */
static void add_length(struct text *t, int n)
{
	t->len += (size_t)n;
	if (t->len >= sizeof(t->buf)) {
		t->len = sizeof(t->buf) - 1;
	}
}

static void make_sysstat(struct text *t)
{
	size_t processes = process_list(ps_info, PS_MAX);
	t->len = 0;
	add_length(t, ksnprintf(t->buf, sizeof(t->buf),
	                           "version %s\n"
	                           "uptime %lu\n"
	                           "idle %lu\n"
	                           "memory %lu %lu\n"
	                           "processes %lu\n"
	                           "cpu %s\n",
	                           MANIOS_VERSION, (uint32_t)timer_uptime_ms(),
	                           (uint32_t)(sched_idle_ticks() * (1000 / TIMER_HZ)),
	                           (uint32_t)pmm_usable_frames() * 4, (uint32_t)pmm_free_frames() * 4,
	                           (uint32_t)processes, cpu_name));
}

static void make_ps(struct text *t)
{
	size_t n = process_list(ps_info, PS_MAX);
	t->len = 0;
	/* The list is newest first: oldest first reads better. */
	while (n-- > 0) {
		const struct process_info *p = &ps_info[n];
		add_length(t, ksnprintf(t->buf + t->len, sizeof(t->buf) - t->len, "%lu %lu %s %lu %lu %s\n",
		                        p->pid, p->ppid, p->state, p->cpu_ms, p->mem_kib, p->name));
	}
}

static long sysstat_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	return read_text(&sysstat_text, make_sysstat, offset, buf, len);
}

static long ps_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	return read_text(&ps_text, make_ps, offset, buf, len);
}

static const struct char_device_ops sysstat_ops = { .pread = sysstat_read };
static const struct char_device_ops ps_ops = { .pread = ps_read };
static struct device sysstat_device = {
	.name = "sysstat", .class = DEVICE_CHAR, .char_ops = &sysstat_ops
};
static struct device ps_device = { .name = "ps", .class = DEVICE_CHAR, .char_ops = &ps_ops };

void sysstat_register(void)
{
	cpu_describe(cpu_name, sizeof(cpu_name));
	device_register(&sysstat_device);
	device_register(&ps_device);
}
