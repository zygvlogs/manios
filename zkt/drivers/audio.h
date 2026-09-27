/* Device "audio": sound out, as 16-bit signed little-endian stereo
 * samples at 44,100 Hz (a CD's format), written to it. A sound card's
 * driver gives the audio layer a ring of AUDIO_FRAGMENTS fragments in
 * memory its card reads by DMA, plays them in turn round the ring from
 * the first once started, and calls audio_fragment_done() from its
 * interrupt each time one has played. Writes fill the fragments (and
 * wait for room); a fragment the writer hasn't filled in time plays as
 * silence. Once the last writer closes, what is left plays and the card
 * stops. */
#ifndef ZKT_DRIVERS_AUDIO_H
#define ZKT_DRIVERS_AUDIO_H

#include <stdint.h>

#define AUDIO_RATE 44100
#define AUDIO_FRAGMENTS 4
#define AUDIO_FRAGMENT_BYTES 4096 /* 1024 stereo samples: 23 ms */
#define AUDIO_BUFFER_BYTES (AUDIO_FRAGMENTS * AUDIO_FRAGMENT_BYTES)

struct audio_card {
	const char *name; /* "Sound Blaster 16" */
	/* Plays the ring from fragment 0, round and round. */
	void (*start)(void);
	void (*stop)(void);
};

/* Registers device "audio" for the card, whose ring (AUDIO_BUFFER_BYTES,
 * zeroed) is at `ring`. Only the first card is used: -EBUSY after. */
int audio_register(const struct audio_card *card, uint8_t *ring);
/* From the card's interrupt handler: a fragment has played. */
void audio_fragment_done(void);

#endif
