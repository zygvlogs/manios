/* fm [FREQUENCY MILLISECONDS]... -- plays notes on the FM synthesizer of
 * an AdLib card or a Sound Blaster (/dev/opl, a Yamaha OPL2); a
 * frequency of 0 is a rest. Without notes, a C major scale.
 *
 * One voice (channel 0): its carrier operator loud, with a quick attack
 * and a medium release, and its modulator silent -- a pure tone, like a
 * flute's. A note's pitch is its F-number and block: the
 * frequency is fnum * 49716 / 2^(20 - block), 49,716 Hz being the
 * chip's sample rate (its 14.318 MHz clock / 288). */
#include <errno.h>
#include <manios.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opl = -1;

static void reg(uint8_t r, uint8_t v)
{
	uint8_t pair[2] = { r, v };
	if (write(opl, pair, 2) != 2) {
		fprintf(stderr, "fm: /dev/opl: %s\n", strerror(errno));
		exit(1);
	}
}

static void note(uint32_t hz, uint32_t ms)
{
	if (hz) {
		/* The lowest block that fits the F-number in its 10 bits. */
		uint32_t block = 0, fnum = 0;
		for (block = 0; block < 8; block++) {
			fnum = (uint32_t)(((uint64_t)hz << (20 - block)) / 49716);
			if (fnum < 1024) {
				break;
			}
		}
		if (block == 8) {
			fprintf(stderr, "fm: %lu Hz is too high\n", (unsigned long)hz);
			exit(1);
		}
		reg(0xA0, (uint8_t)fnum);
		reg(0xB0, (uint8_t)(0x20 | block << 2 | fnum >> 8)); /* key on */
	}
	sleep_ms(ms);
	reg(0xB0, 0); /* key off */
}

int main(int argc, char **argv)
{
	static const char *const SCALE[] = { "262", "200", "294", "200", "330", "200", "349", "200",
	                                     "392", "200", "440", "200", "494", "200", "523", "400" };
	if (argc % 2 == 0) {
		fprintf(stderr, "usage: fm [FREQUENCY MILLISECONDS]...\n");
		return 1;
	}
	opl = open("/dev/opl", OWRITE);
	if (opl < 0) {
		fprintf(stderr, "fm: /dev/opl: %s\n", errno == ENOENT ? "no FM synthesizer" : strerror(errno));
		return 1;
	}
	reg(0x20, 0x01); /* modulator: multiple 1 */
	reg(0x40, 0x3F); /* modulator level: silent -- a pure tone */
	reg(0x60, 0xF4); /* attack fast, decay slow */
	reg(0x80, 0x77); /* sustain, release */
	reg(0x23, 0x01); /* carrier: multiple 1 */
	reg(0x43, 0x00); /* carrier level: loudest */
	reg(0x63, 0xF4);
	reg(0x83, 0x77);
	reg(0xC0, 0x00); /* FM, no feedback */
	const char *const *notes = argc > 1 ? (const char *const *)argv + 1 : SCALE;
	int count = argc > 1 ? argc - 1 : (int)(sizeof(SCALE) / sizeof(SCALE[0]));
	for (int i = 0; i + 1 < count; i += 2) {
		char *end1, *end2;
		unsigned long hz = strtoul(notes[i], &end1, 10), ms = strtoul(notes[i + 1], &end2, 10);
		if (*end1 || *end2 || hz > 6000 || ms > 600000) {
			fprintf(stderr, "fm: not a frequency and a length: %s %s\n", notes[i], notes[i + 1]);
			return 1;
		}
		note((uint32_t)hz, (uint32_t)ms);
	}
	return 0;
}
