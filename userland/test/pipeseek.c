/* Seeks a pipe and tells what happened in its exit status: 0 if the
 * seek "succeeded" (what ABI version 1 promised), 1 if it failed with
 * ESPIPE (version 2), 2 otherwise. Built twice: as itself, and as
 * pipeseek1 with a version-1 ABI note (Makefile), so utest can check
 * that the kernel answers each version as it promised (zkt_abi.h). */
#include <errno.h>
#include <manios.h>

int main(void)
{
	int fds[2];
	if (pipe(fds) < 0) {
		return 3;
	}
	if (lseek(fds[0], 0, SEEK_CUR) >= 0) {
		return 0;
	}
	return errno == ESPIPE ? 1 : 2;
}
