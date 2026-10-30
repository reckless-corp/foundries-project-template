/* SPDX-License-Identifier: Apache-2.0 */
#define main matrixctl_main
#include "../controller/matrixctl.c"
#undef main
#include <assert.h>

static void check_line(const char *input, size_t size, int expected,
		       const char *prefix)
{
	int fds[2];
	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	assert(send_all(fds[0], input, now_ms() + 1000) == 0);
	assert(send_all(fds[0], "NEXT\n", now_ms() + 1000) == 0);

	unsigned char storage[16];
	memset(storage, 0xa5, sizeof(storage));
	char *line = (char *)storage + 1;
	assert(read_line(fds[1], line, size, now_ms() + 1000) == expected);
	assert(storage[0] == 0xa5);
	for (size_t i = size + 1; i < sizeof(storage); ++i)
		assert(storage[i] == 0xa5);
	if (size)
		assert(strcmp(line, prefix) == 0);

	/* A rejected line must be consumed through its newline. */
	char next[5];
	assert(read_line(fds[1], next, sizeof(next), now_ms() + 1000) == 0);
	assert(strcmp(next, "NEXT") == 0);
	close(fds[0]);
	close(fds[1]);
}

int main(void)
{
	check_line("AB\n", 4, 0, "AB");
	check_line("ABC\n", 4, 0, "ABC");
	check_line("ABCD\n", 4, -2, "ABC");
	check_line("ABCDEFGHIJ\n", 4, -2, "ABC");
	check_line("\n", 1, 0, "");
	check_line("A\n", 1, -2, "");
	check_line("\n", 0, -2, NULL);
	check_line("ABCD\n", 0, -2, NULL);
	check_line("A\tBC\n", 4, -2, "A");
	return 0;
}
