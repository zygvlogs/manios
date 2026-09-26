/* Signals: ManiOS has none. signal() records a handler, which is never
 * called; raise() does what the default action would for the few
 * signals programs raise themselves. (A write to a pipe with no reader
 * ends a program through stdio instead: stdio.c.) */
#ifndef MANIOS_SIGNAL_H
#define MANIOS_SIGNAL_H

typedef int sig_atomic_t;
typedef void (*sig_t)(int);

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGABRT 6
#define SIGKILL 9
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 20
#define SIGWINCH 28
#define SIGINFO 29
#define NSIG 33

#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)
#define SIG_ERR ((sig_t)-1)

sig_t signal(int sig, sig_t handler);
int raise(int sig);

#endif
