/* ManiOS has one locale, "C": characters are bytes. */
#ifndef MANIOS_LOCALE_H
#define MANIOS_LOCALE_H

#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
#define LC_MESSAGES 6

/* "C" for "C", "POSIX" and "" (the default); NULL for anything else. */
char *setlocale(int category, const char *locale);

#endif
