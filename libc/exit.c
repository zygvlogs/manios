#include <manios.h>

__attribute__((noreturn)) void exit(int code)
{
	_exit(code);
}
