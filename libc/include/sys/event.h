/* kqueue, the BSDs' event notification: ManiOS has none. kqueue() fails
 * with ENOSYS, and programs go without (tail -f says so and stops). */
#ifndef MANIOS_SYS_EVENT_H
#define MANIOS_SYS_EVENT_H

#include <stdint.h>
#include <time.h>

struct kevent {
	uintptr_t ident;
	short filter;
	unsigned short flags;
	unsigned int fflags;
	int64_t data;
	void *udata;
};

#define EV_SET(kevp, a, b, c, d, e, f) do {	\
	struct kevent *__kevp = (kevp);		\
	__kevp->ident = (a);			\
	__kevp->filter = (b);			\
	__kevp->flags = (c);			\
	__kevp->fflags = (d);			\
	__kevp->data = (e);			\
	__kevp->udata = (f);			\
} while (0)

#define EVFILT_READ  (-1)
#define EVFILT_WRITE (-2)
#define EVFILT_VNODE (-4)
#define EV_ADD     0x0001
#define EV_DELETE  0x0002
#define EV_ENABLE  0x0004
#define EV_DISABLE 0x0008
#define EV_ONESHOT 0x0010
#define EV_CLEAR   0x0020
#define EV_EOF     0x8000
#define EV_ERROR   0x4000
#define NOTE_DELETE   0x0001
#define NOTE_WRITE    0x0002
#define NOTE_EXTEND   0x0004
#define NOTE_ATTRIB   0x0008
#define NOTE_LINK     0x0010
#define NOTE_RENAME   0x0020
#define NOTE_TRUNCATE 0x0080

int kqueue(void);
int kevent(int kq, const struct kevent *changes, int nchanges, struct kevent *events,
           int nevents, const struct timespec *timeout);

#endif
