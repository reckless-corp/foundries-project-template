/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

const char *hat_parse_command(const char *s, struct hat_command *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	if (!strcmp(s, "HELLO"))
		cmd->operation = HAT_HELLO;
	else if (!strcmp(s, "PING"))
		cmd->operation = HAT_PING;
	else if (!strcmp(s, "SHOW HAT"))
		cmd->operation = HAT_SHOW;
	else if (!strcmp(s, "DISPLAY ON") || !strcmp(s, "DISPLAY OFF")) {
		cmd->operation = HAT_DISPLAY;
		cmd->visible = !strcmp(s, "DISPLAY ON");
	} else if (!strncmp(s, "FRAME ", 6)) {
		cmd->operation = HAT_FRAME;
		if (strlen(s + 6) != HAT_FRAME_SIZE * 2)
			return "BAD_ARGUMENT";
		for (size_t i = 0; i < HAT_FRAME_SIZE; ++i) {
			int high = hex(s[6 + i * 2]), low = hex(s[7 + i * 2]);
			if (high < 0 || low < 0)
				return "BAD_ARGUMENT";
			cmd->pixels[i] = (uint8_t)((high << 4) | low);
			if ((i & 1) && (cmd->pixels[i] & 0xe0))
				return "BAD_ARGUMENT";
		}
	} else {
		/* Known verbs with missing/extra arguments are distinguishable.
		 */
		static const char *const verbs[] = {"HELLO", "PING", "SHOW",
						    "DISPLAY", "FRAME"};
		for (size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); ++i) {
			size_t n = strlen(verbs[i]);
			if (!strncmp(s, verbs[i], n) &&
			    (s[n] == 0 || s[n] == ' '))
				return "BAD_ARGUMENT";
		}
		return "UNKNOWN_COMMAND";
	}
	return NULL;
}

void hat_resync(struct hat_parser *p)
{
	p->used = 0;
	p->discard = true;
}

bool hat_feed(struct hat_parser *p, unsigned char c, char *reply, size_t size,
	      hat_handler handler, void *context)
{
	if (c != '\n') {
		if (!p->discard) {
			if (p->used == HAT_LINE_MAX ||
			    (c != '\r' && (c < 32 || c > 126)))
				hat_resync(p);
			else
				p->line[p->used++] = (char)c;
		}
		return false;
	}
	uint32_t id = 0;
	const char *error = NULL;
	char result[96] = "";
	if (p->discard)
		error = "BAD_LINE";
	else {
		if (p->used && p->line[p->used - 1] == '\r')
			--p->used;
		p->line[p->used] = 0;
		const char *s = p->line;
		if (*s < '1' || *s > '9')
			error = "BAD_ID";
		while (!error && *s >= '0' && *s <= '9') {
			unsigned digit = (unsigned)(*s++ - '0');
			if (id > (UINT32_MAX - digit) / 10)
				error = "BAD_ID";
			else
				id = id * 10 + digit;
		}
		if (error || *s != ' ') {
			id = 0;
			error = "BAD_ID";
		} else {
			struct hat_command cmd;
			error = hat_parse_command(s + 1, &cmd);
			if (!error &&
			    handler(&cmd, result, sizeof(result), context))
				error = "IO";
		}
	}
	p->used = 0;
	p->discard = false;
	if (error)
		snprintf(reply, size, "%" PRIu32 " ERR %s\n", id, error);
	else
		snprintf(reply, size, "%" PRIu32 " OK%s%s\n", id,
			 *result ? " " : "", result);
	return true;
}
