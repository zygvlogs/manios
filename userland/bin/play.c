/* play FILE.WAV... -- plays WAV files on the sound card (/dev/audio);
 * play -t FREQUENCY MILLISECONDS... -- plays sine tones (0 is a rest).
 *
 * /dev/audio takes 16-bit signed stereo at 44,100 Hz. WAV files of
 * uncompressed PCM, 8- or 16-bit, mono or stereo, at any rate, are
 * made that: 8-bit samples are unsigned and widened, mono is doubled,
 * and other rates are taken to 44,100 Hz by repeating or skipping
 * samples (the nearest one). Tones are made with a sine table and
 * integer arithmetic (ManiOS's programs have no floating point), and
 * fade in and out over 5 ms, so they don't click. */
#include <errno.h>
#include <manios.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define AMPLITUDE 16384 /* half of full scale */
#define FADE (RATE / 200)

static int audio = -1;
static int16_t out[2048]; /* 1024 stereo samples */
static size_t out_n;

/* sin(2 pi i / 256) * 32767, for i = 0..256. */
static const int16_t SINE[257] = {
	0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
	9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
	18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
	25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
	30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
	32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
	32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
	28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
	23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151,
	15446, 14732, 14010, 13279, 12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
	6393, 5602, 4808, 4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410,
	-3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
	-12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
	-20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
	-27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
	-31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
	-32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
	-31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
	-27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
	-20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
	-12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011,
	-3212, -2410, -1608, -804, 0,

};

static int flush(void)
{
	long want = (long)(out_n * sizeof(out[0]));
	if (out_n && write(audio, out, (size_t)want) != want) {
		fprintf(stderr, "play: /dev/audio: %s\n", strerror(errno));
		return -1;
	}
	out_n = 0;
	return 0;
}

static int put(int16_t left, int16_t right)
{
	out[out_n++] = left;
	out[out_n++] = right;
	return out_n == sizeof(out) / sizeof(out[0]) ? flush() : 0;
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int tone(uint32_t hz, uint32_t ms)
{
	uint32_t samples = (uint32_t)((uint64_t)ms * RATE / 1000);
	uint32_t step = (uint32_t)(((uint64_t)hz << 32) / RATE), phase = 0;
	for (uint32_t i = 0; i < samples; i++) {
		int32_t v = 0;
		if (hz) {
			uint32_t index = phase >> 24, frac = (phase >> 8) & 0xFFFF;
			int32_t a = SINE[index], b = SINE[index + 1];
			v = a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
			v = (int32_t)((int64_t)v * AMPLITUDE / 32767);
			uint32_t edge = i < samples - i ? i : samples - i;
			if (edge < FADE) {
				v = v * (int32_t)edge / FADE;
			}
			phase += step;
		}
		if (put((int16_t)v, (int16_t)v) < 0) {
			return -1;
		}
	}
	return 0;
}

static int play_file(const char *path)
{
	int fd = open(path, OREAD);
	if (fd < 0) {
		fprintf(stderr, "play: %s: %s\n", path, strerror(errno));
		return -1;
	}
	uint8_t head[12], chunk[8], fmt[16];
	int channels = 0, bits = 0;
	uint32_t rate = 0, data = 0;
	int rc = -1;
	if (read(fd, head, 12) != 12 || memcmp(head, "RIFF", 4) || memcmp(head + 8, "WAVE", 4)) {
		fprintf(stderr, "play: %s: not a WAV file\n", path);
		goto done;
	}
	/* The chunks: "fmt " says what the samples are, "data" is them. */
	while (read(fd, chunk, 8) == 8) {
		uint32_t size = le32(chunk + 4);
		if (!memcmp(chunk, "fmt ", 4) && size >= 16) {
			if (read(fd, fmt, 16) != 16) {
				break;
			}
			if (le16(fmt) != 1) {
				fprintf(stderr, "play: %s: compressed (format %u), not PCM\n", path, le16(fmt));
				goto done;
			}
			channels = le16(fmt + 2);
			rate = le32(fmt + 4);
			bits = le16(fmt + 14);
			size -= 16;
		} else if (!memcmp(chunk, "data", 4)) {
			data = size;
			break;
		}
		if (lseek(fd, (long)(size + (size & 1)), SEEK_CUR) < 0) {
			break;
		}
	}
	if (!data || channels < 1 || channels > 2 || (bits != 8 && bits != 16) || rate < 1000
	    || rate > 192000) {
		fprintf(stderr, "play: %s: not PCM of 8 or 16 bits, mono or stereo\n", path);
		goto done;
	}
	/* Input frames per output frame, in 16.16 fixed point. */
	uint32_t step = (uint32_t)(((uint64_t)rate << 16) / RATE), pos = 0;
	int frame = channels * bits / 8;
	uint8_t buf[4096], sample[4] = { 0 };
	uint32_t frames = data / (uint32_t)frame, have = 0, next = 0;
	long got = 0, used = 0;
	while (next < frames) {
		/* Read frames up to the one this output sample takes. */
		while (have <= next) {
			if (used == got) {
				got = read(fd, buf, sizeof(buf) - sizeof(buf) % (size_t)frame);
				used = 0;
				if (got <= 0) {
					goto flushed;
				}
			}
			memcpy(sample, buf + used, (size_t)frame);
			used += frame;
			have++;
		}
		int16_t l, r;
		if (bits == 8) {
			l = (int16_t)((sample[0] - 128) << 8);
			r = channels == 2 ? (int16_t)((sample[1] - 128) << 8) : l;
		} else {
			l = (int16_t)le16(sample);
			r = channels == 2 ? (int16_t)le16(sample + 2) : l;
		}
		if (put(l, r) < 0) {
			goto done;
		}
		pos += step;
		next = pos >> 16;
	}
flushed:
	rc = 0;
done:
	close(fd);
	return rc;
}

int main(int argc, char **argv)
{
	int tones = argc > 1 && !strcmp(argv[1], "-t");
	if (argc < 2 || (tones && (argc - 2) % 2)) {
		fprintf(stderr, "usage: play FILE.WAV...\n       play -t FREQUENCY MILLISECONDS...\n");
		return 1;
	}
	audio = open("/dev/audio", OWRITE);
	if (audio < 0) {
		fprintf(stderr, "play: /dev/audio: %s\n", errno == ENOENT ? "no sound card" : strerror(errno));
		return 1;
	}
	int failed = 0;
	for (int i = tones ? 2 : 1; i < argc; i += tones ? 2 : 1) {
		if (tones) {
			char *end1, *end2;
			unsigned long hz = strtoul(argv[i], &end1, 10), ms = strtoul(argv[i + 1], &end2, 10);
			if (*end1 || *end2 || hz > 20000 || ms > 600000) {
				fprintf(stderr, "play: not a frequency and a length: %s %s\n", argv[i], argv[i + 1]);
				return 1;
			}
			failed |= tone((uint32_t)hz, (uint32_t)ms) < 0;
		} else {
			failed |= play_file(argv[i]) < 0;
		}
	}
	failed |= flush() < 0;
	close(audio); /* waits for the sound to play out */
	return failed;
}
