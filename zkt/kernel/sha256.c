/* SHA-256 as specified in FIPS 180-4 §6.2, and HMAC (RFC 2104). */
#include "sha256.h"
#include "kstring.h"

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n)
{
	return x >> n | x << (32 - n);
}

static void compress(struct sha256 *c, const uint8_t *p)
{
	uint32_t w[64];
	for (int t = 0; t < 16; t++) {
		w[t] = (uint32_t)p[4 * t] << 24 | (uint32_t)p[4 * t + 1] << 16 | (uint32_t)p[4 * t + 2] << 8
		       | p[4 * t + 3];
	}
	for (int t = 16; t < 64; t++) {
		uint32_t s0 = ror(w[t - 15], 7) ^ ror(w[t - 15], 18) ^ w[t - 15] >> 3;
		uint32_t s1 = ror(w[t - 2], 17) ^ ror(w[t - 2], 19) ^ w[t - 2] >> 10;
		w[t] = w[t - 16] + s0 + w[t - 7] + s1;
	}
	uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
	uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
	for (int t = 0; t < 64; t++) {
		uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
		uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & cc) ^ (b & cc));
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = cc;
		cc = b;
		b = a;
		a = t1 + t2;
	}
	c->h[0] += a;
	c->h[1] += b;
	c->h[2] += cc;
	c->h[3] += d;
	c->h[4] += e;
	c->h[5] += f;
	c->h[6] += g;
	c->h[7] += h;
}

void sha256_init(struct sha256 *c)
{
	static const uint32_t H0[8] = {
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};
	memcpy(c->h, H0, sizeof(H0));
	c->used = 0;
	c->bits_lo = c->bits_hi = 0;
}

void sha256_update(struct sha256 *c, const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t add = (uint32_t)len << 3;
	c->bits_hi += (uint32_t)(len >> 29) + (c->bits_lo + add < c->bits_lo);
	c->bits_lo += add;
	while (len) {
		size_t n = SHA256_BLOCK - c->used;
		if (n > len) {
			n = len;
		}
		memcpy(c->block + c->used, p, n);
		c->used += (uint32_t)n;
		p += n;
		len -= n;
		if (c->used == SHA256_BLOCK) {
			compress(c, c->block);
			c->used = 0;
		}
	}
}

void sha256_final(struct sha256 *c, uint8_t out[SHA256_BYTES])
{
	uint32_t lo = c->bits_lo, hi = c->bits_hi;
	uint8_t pad = 0x80, zero = 0, length[8];
	sha256_update(c, &pad, 1);
	while (c->used != SHA256_BLOCK - 8) {
		sha256_update(c, &zero, 1);
	}
	for (int i = 0; i < 4; i++) {
		length[i] = (uint8_t)(hi >> (24 - 8 * i));
		length[4 + i] = (uint8_t)(lo >> (24 - 8 * i));
	}
	sha256_update(c, length, 8);
	for (int i = 0; i < 8; i++) {
		for (int k = 0; k < 4; k++) {
			out[4 * i + k] = (uint8_t)(c->h[i] >> (24 - 8 * k));
		}
	}
	memset(c, 0, sizeof(*c));
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_BYTES])
{
	struct sha256 c;
	sha256_init(&c);
	sha256_update(&c, data, len);
	sha256_final(&c, out);
}

void hmac_sha256_init(struct hmac_sha256 *h, const void *key, size_t key_len)
{
	uint8_t k[SHA256_BLOCK], inner_key[SHA256_BLOCK];
	memset(k, 0, sizeof(k));
	if (key_len > SHA256_BLOCK) {
		sha256(key, key_len, k);
	} else {
		memcpy(k, key, key_len);
	}
	for (int i = 0; i < SHA256_BLOCK; i++) {
		inner_key[i] = k[i] ^ 0x36;
		h->outer_key[i] = k[i] ^ 0x5c;
	}
	sha256_init(&h->inner);
	sha256_update(&h->inner, inner_key, sizeof(inner_key));
	memset(k, 0, sizeof(k));
	memset(inner_key, 0, sizeof(inner_key));
}

void hmac_sha256_update(struct hmac_sha256 *h, const void *data, size_t len)
{
	sha256_update(&h->inner, data, len);
}

void hmac_sha256_final(struct hmac_sha256 *h, uint8_t out[SHA256_BYTES])
{
	uint8_t inner[SHA256_BYTES];
	sha256_final(&h->inner, inner);
	struct sha256 outer;
	sha256_init(&outer);
	sha256_update(&outer, h->outer_key, sizeof(h->outer_key));
	sha256_update(&outer, inner, sizeof(inner));
	sha256_final(&outer, out);
	memset(h, 0, sizeof(*h));
}

bool equal_secret(const void *a, const void *b, size_t len)
{
	const volatile uint8_t *x = a, *y = b;
	uint8_t diff = 0;
	for (size_t i = 0; i < len; i++) {
		diff |= x[i] ^ y[i];
	}
	return diff == 0;
}

static bool hex_equal(const uint8_t *digest, const char *hex)
{
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < SHA256_BYTES; i++) {
		if (hex[2 * i] != digits[digest[i] >> 4] || hex[2 * i + 1] != digits[digest[i] & 15]) {
			return false;
		}
	}
	return true;
}

bool sha256_selftest(void)
{
	uint8_t d[SHA256_BYTES];
	bool ok = true;
	/* FIPS 180-4 examples: one block, two blocks; and the empty message. */
	sha256("abc", 3, d);
	ok &= hex_equal(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	sha256(two, strlen(two), d);
	ok &= hex_equal(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	sha256("", 0, d);
	ok &= hex_equal(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	/* A million 'a's, fed in uneven pieces: the length crosses many blocks. */
	struct sha256 c;
	char as[97];
	memset(as, 'a', sizeof(as));
	sha256_init(&c);
	size_t fed = 0;
	for (size_t piece = 1; fed < 1000000; piece = piece % 97 + 1) {
		size_t n = 1000000 - fed < piece ? 1000000 - fed : piece;
		sha256_update(&c, as, n);
		fed += n;
	}
	sha256_final(&c, d);
	ok &= hex_equal(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	/* RFC 4231 test cases 1 and 2, and 6 (a key longer than a block). */
	struct hmac_sha256 h;
	uint8_t key[131];
	memset(key, 0x0b, 20);
	hmac_sha256_init(&h, key, 20);
	hmac_sha256_update(&h, "Hi There", 8);
	hmac_sha256_final(&h, d);
	ok &= hex_equal(d, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
	hmac_sha256_init(&h, "Jefe", 4);
	const char *what = "what do ya want for nothing?";
	hmac_sha256_update(&h, what, strlen(what));
	hmac_sha256_final(&h, d);
	ok &= hex_equal(d, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
	memset(key, 0xaa, sizeof(key));
	hmac_sha256_init(&h, key, sizeof(key));
	const char *big = "Test Using Larger Than Block-Size Key - Hash Key First";
	hmac_sha256_update(&h, big, strlen(big));
	hmac_sha256_final(&h, d);
	ok &= hex_equal(d, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
	ok &= equal_secret("same", "same", 4) && !equal_secret("same", "sane", 4);
	return ok;
}
