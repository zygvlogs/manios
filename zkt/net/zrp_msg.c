/* ZRP message encoding and decoding (docs/zrp.md). */
#include "zrp.h"
#include "kstring.h"
#include "zkt_abi.h"

void zmsg_start(struct zmsg *m, uint8_t *buf, size_t cap, uint8_t type, uint16_t tag)
{
	m->buf = buf;
	m->cap = cap;
	m->pos = 4; /* the size comes last */
	m->bad = cap < ZRP_HEADER;
	zmsg_put8(m, type);
	zmsg_put16(m, tag);
}

size_t zmsg_finish(struct zmsg *m)
{
	if (m->bad) {
		return 0;
	}
	uint32_t size = (uint32_t)m->pos;
	for (int i = 0; i < 4; i++) {
		m->buf[i] = (uint8_t)(size >> (8 * i));
	}
	return m->pos;
}

static uint8_t *room(struct zmsg *m, size_t n)
{
	if (m->bad || n > m->cap - m->pos) {
		m->bad = true;
		return 0;
	}
	uint8_t *p = m->buf + m->pos;
	m->pos += n;
	return p;
}

void zmsg_put8(struct zmsg *m, uint8_t v)
{
	uint8_t *p = room(m, 1);
	if (p) {
		p[0] = v;
	}
}

void zmsg_put16(struct zmsg *m, uint16_t v)
{
	uint8_t *p = room(m, 2);
	if (p) {
		p[0] = (uint8_t)v;
		p[1] = (uint8_t)(v >> 8);
	}
}

void zmsg_put32(struct zmsg *m, uint32_t v)
{
	uint8_t *p = room(m, 4);
	if (p) {
		for (int i = 0; i < 4; i++) {
			p[i] = (uint8_t)(v >> (8 * i));
		}
	}
}

void zmsg_putbytes(struct zmsg *m, const void *data, size_t len)
{
	uint8_t *p = room(m, len);
	if (p) {
		memcpy(p, data, len);
	}
}

void zmsg_putstr(struct zmsg *m, const char *s)
{
	size_t len = strlen(s);
	if (len > 0xFFFF) {
		m->bad = true;
		return;
	}
	zmsg_put16(m, (uint16_t)len);
	zmsg_putbytes(m, s, len);
}

void zmsg_putstat(struct zmsg *m, enum vnode_type type, uint32_t length, const char *name)
{
	zmsg_put8(m, type == VNODE_DIR ? ZKT_TYPE_DIR : type == VNODE_DEVICE ? ZKT_TYPE_DEVICE
	                                                                     : ZKT_TYPE_FILE);
	zmsg_put32(m, length);
	zmsg_putstr(m, name);
}

void zmsg_open(struct zmsg *m, uint8_t *buf, size_t len)
{
	m->buf = buf;
	m->cap = len;
	m->pos = 0;
	m->bad = false;
}

uint8_t zmsg_get8(struct zmsg *m)
{
	uint8_t *p = room(m, 1);
	return p ? p[0] : 0;
}

uint16_t zmsg_get16(struct zmsg *m)
{
	uint8_t *p = room(m, 2);
	return p ? (uint16_t)(p[0] | p[1] << 8) : 0;
}

uint32_t zmsg_get32(struct zmsg *m)
{
	uint8_t *p = room(m, 4);
	return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}

const uint8_t *zmsg_getbytes(struct zmsg *m, size_t len)
{
	return room(m, len);
}

void zmsg_getstr(struct zmsg *m, char *out, size_t size)
{
	uint16_t len = zmsg_get16(m);
	const uint8_t *p = zmsg_getbytes(m, len);
	if (!p || len >= size) {
		m->bad = true;
		if (size) {
			out[0] = '\0';
		}
		return;
	}
	memcpy(out, p, len);
	out[len] = '\0';
}

bool zmsg_getstat(struct zmsg *m, enum vnode_type *type, uint32_t *length, char *name, size_t size)
{
	uint8_t t = zmsg_get8(m);
	*length = zmsg_get32(m);
	zmsg_getstr(m, name, size);
	if (t == ZKT_TYPE_DIR) {
		*type = VNODE_DIR;
	} else if (t == ZKT_TYPE_FILE) {
		*type = VNODE_FILE;
	} else if (t == ZKT_TYPE_DEVICE) {
		*type = VNODE_DEVICE;
	} else {
		m->bad = true;
	}
	return !m->bad;
}
