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

/* Strict unsigned decimal, with overflow and delimiter checks by the caller. */
static bool number(const char **cursor, uint32_t *value)
{
	const char *s = *cursor;
	uint32_t n = 0;
	if (*s < '0' || *s > '9')
		return false;
	while (*s >= '0' && *s <= '9') {
		unsigned d = (unsigned)(*s++ - '0');
		if (n > (UINT32_MAX - d) / 10)
			return false;
		n = n * 10 + d;
	}
	*cursor = s;
	*value = n;
	return true;
}

static bool pixels(const char *s, uint8_t *out)
{
	if (strlen(s) < HAT_FRAME_SIZE * 2)
		return false;
	for (size_t i = 0; i < HAT_FRAME_SIZE; ++i) {
		int hi = hex(s[i * 2]), lo = hex(s[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
		if ((i & 1) && (out[i] & 0xe0))
			return false;
	}
	return true;
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
		if (!pixels(s + 6, cmd->pixels))
			return "BAD_ARGUMENT";
	} else if (!strcmp(s, "ANIM STOP")) {
		cmd->operation = HAT_ANIM_STOP;
	} else if (!strncmp(s, "ANIM BEGIN ", 11) ||
		   !strncmp(s, "ANIM PLAY ", 10)) {
		bool begin = s[5] == 'B';
		s += begin ? 11 : 10;
		cmd->operation = begin ? HAT_ANIM_BEGIN : HAT_ANIM_PLAY;
		if (!number(&s, &cmd->value) || *s ||
		    (begin && (!cmd->value || cmd->value > HAT_ANIM_MAX)))
			return "BAD_ARGUMENT";
	} else if (!strncmp(s, "ANIM ADD ", 9)) {
		cmd->operation = HAT_ANIM_ADD;
		s += 9;
		if (!number(&s, &cmd->value) || cmd->value < 20 ||
		    cmd->value > 60000 || *s != ' ' || strlen(s + 1) != 32 ||
		    !pixels(s + 1, cmd->pixels))
			return "BAD_ARGUMENT";
	} else {
		/* Known verbs with missing/extra arguments are distinguishable.
		 */
		static const char *const verbs[] = {"HELLO",   "PING",	"SHOW",
						    "DISPLAY", "FRAME", "ANIM"};
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

const char *hat_parse_client(const char *s, struct hat_command *cmd,
			     struct hat_animation *animation)
{
	if (strncmp(s, "ANIM ", 5) || !strcmp(s, "ANIM STOP"))
		return hat_parse_command(s, cmd);
	memset(cmd, 0, sizeof(*cmd));
	memset(animation, 0, sizeof(*animation));
	cmd->operation = HAT_ANIM_PLAY;
	s += 5;
	if (!number(&s, &animation->repeats) || *s != ' ')
		return "BAD_ARGUMENT";
	while (*s == ' ') {
		++s;
		if (animation->count == HAT_ANIM_MAX)
			return "BAD_ARGUMENT";
		struct hat_step *step = &animation->steps[animation->count];
		if (!number(&s, &step->duration_ms) || step->duration_ms < 20 ||
		    step->duration_ms > 60000 || *s != ':' ||
		    !pixels(s + 1, step->pixels))
			return "BAD_ARGUMENT";
		s += 33;
		++animation->count;
	}
	return *s || !animation->count ? "BAD_ARGUMENT" : NULL;
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
