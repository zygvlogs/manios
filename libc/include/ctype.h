#ifndef MANIOS_CTYPE_H
#define MANIOS_CTYPE_H

/* ASCII only. Arguments are an unsigned char's value, or EOF. */
int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int isxdigit(int c);
int islower(int c);
int isupper(int c);
int isspace(int c);
int isblank(int c);
int isprint(int c);
int isgraph(int c);
int isascii(int c);
int ispunct(int c);
int iscntrl(int c);
int tolower(int c);
int toupper(int c);

#endif
