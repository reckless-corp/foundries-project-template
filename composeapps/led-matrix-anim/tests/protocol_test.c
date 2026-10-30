/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;
static struct hat_command last;
static int fail;
static int execute(const struct hat_command *cmd, char *out, size_t size,
		   void *ctx)
{
	assert(ctx == &calls);
	++calls;
	last = *cmd;
	snprintf(out, size, "done");
	return fail;
}
static void check(struct hat_parser *p, const char *wire, const char *expected)
{
	char reply[HAT_LINE_MAX + 1] = "";
	unsigned replies = 0;
	for (const unsigned char *s = (const unsigned char *)wire; *s; ++s) {
		if (hat_feed(p, *s, reply, sizeof(reply), execute, &calls))
			++replies;
	}
	assert(replies == 1);
	if (strcmp(reply, expected)) {
		fprintf(stderr, "wire=%s expected=%s actual=%s", wire, expected,
			reply);
		assert(0);
	}
}
int main(void)
{
	struct hat_parser p = {0};
	check(&p, "1 HELLO\n", "1 OK done\n");
	assert(last.operation == HAT_HELLO);
	check(&p, "4294967295 PING\r\n", "4294967295 OK done\n");
	assert(last.operation == HAT_PING);
	check(&p, "2 SHOW HAT\n", "2 OK done\n");
	assert(last.operation == HAT_SHOW);
	check(&p, "3 DISPLAY OFF\n", "3 OK done\n");
	assert(last.operation == HAT_DISPLAY && !last.visible);
	check(&p, "4 DISPLAY ON\n", "4 OK done\n");
	assert(last.visible);
	check(&p, "5 FRAME ff1fff1fff1fff1fff1fff1fff1fff1f\n", "5 OK done\n");
	assert(last.operation == HAT_FRAME && last.pixels[0] == 255 &&
	       last.pixels[15] == 31);
	check(&p, "20 ANIM BEGIN 64\n", "20 OK done\n");
	assert(last.operation == HAT_ANIM_BEGIN && last.value == 64);
	check(&p, "21 ANIM ADD 20 01000000000000000000000000000000\n",
	      "21 OK done\n");
	assert(last.operation == HAT_ANIM_ADD && last.value == 20 &&
	       last.pixels[0] == 1);
	check(&p, "22 ANIM PLAY 4294967295\n", "22 OK done\n");
	assert(last.operation == HAT_ANIM_PLAY && last.value == UINT32_MAX);
	check(&p, "23 ANIM STOP\n", "23 OK done\n");
	assert(last.operation == HAT_ANIM_STOP);
	unsigned before = calls;
	check(&p, "0 PING\n", "0 ERR BAD_ID\n");
	check(&p, "4294967296 PING\n", "0 ERR BAD_ID\n");
	check(&p, "-1 PING\n", "0 ERR BAD_ID\n");
	check(&p, "1PING\n", "0 ERR BAD_ID\n");
	check(&p, "6 FRAME ff3fff1fff1fff1fff1fff1fff1fff1f\n",
	      "6 ERR BAD_ARGUMENT\n");
	check(&p, "6 FRAME gg1fff1fff1fff1fff1fff1fff1fff1f\n",
	      "6 ERR BAD_ARGUMENT\n");
	check(&p, "6 FRAME 00\n", "6 ERR BAD_ARGUMENT\n");
	check(&p, "7 DISPLAY MAYBE\n", "7 ERR BAD_ARGUMENT\n");
	check(&p, "8 HELLO extra\n", "8 ERR BAD_ARGUMENT\n");
	check(&p, "9 TOGGLE\n", "9 ERR UNKNOWN_COMMAND\n");
	check(&p, "9 PI\rNG\n", "9 ERR UNKNOWN_COMMAND\n");
	check(&p, "24 ANIM BEGIN 0\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM BEGIN 65\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM PLAY -1\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM PLAY 4294967296\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM PLAY 1 extra\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM STOP extra\n", "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM ADD 19 01000000000000000000000000000000\n",
	      "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM ADD 60001 01000000000000000000000000000000\n",
	      "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM ADD 20 01ff0000000000000000000000000000\n",
	      "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM ADD 20 0100000000000000000000000000000g\n",
	      "24 ERR BAD_ARGUMENT\n");
	check(&p, "24 ANIM ADD 20 01\n", "24 ERR BAD_ARGUMENT\n");
	assert(calls == before);
	char reply[HAT_LINE_MAX + 1];
	/* Fragmentation, overflow, invalid bytes, and explicit UART resync. */
	assert(!hat_feed(&p, '1', reply, sizeof(reply), execute, &calls));
	check(&p, "0 PING\n", "10 OK done\n");
	before = calls;
	for (int i = 0; i < 300; ++i)
		assert(
		    !hat_feed(&p, 'x', reply, sizeof(reply), execute, &calls));
	check(&p, "11 DISPLAY ON\n", "0 ERR BAD_LINE\n");
	assert(calls == before);
	check(&p, "12 PING\n", "12 OK done\n");
	assert(!hat_feed(&p, 0, reply, sizeof(reply), execute, &calls));
	check(&p, "13 SHOW HAT\n", "0 ERR BAD_LINE\n");
	hat_resync(&p);
	check(&p, "14 PING\n", "0 ERR BAD_LINE\n");
	check(&p, "15 PING\n", "15 OK done\n");
	fail = 1;
	check(&p, "16 SHOW HAT\n", "16 ERR IO\n");
	return 0;
}
