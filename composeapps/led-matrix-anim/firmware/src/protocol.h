/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HAT_PROTOCOL_H
#define HAT_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAT_LINE_MAX 128
#define HAT_FRAME_SIZE 16
#define HAT_ANIM_MAX 64
#define HAT_CLIENT_LINE_MAX 4096
struct hat_step {
	uint32_t duration_ms;
	uint8_t pixels[HAT_FRAME_SIZE];
};
struct hat_animation {
	uint32_t repeats;
	size_t count;
	struct hat_step steps[HAT_ANIM_MAX];
};
enum hat_operation {
	HAT_HELLO,
	HAT_PING,
	HAT_SHOW,
	HAT_DISPLAY,
	HAT_FRAME,
	HAT_ANIM_BEGIN,
	HAT_ANIM_ADD,
	HAT_ANIM_PLAY,
	HAT_ANIM_STOP
};
struct hat_command {
	enum hat_operation operation;
	bool visible;
	uint32_t value;
	uint8_t pixels[HAT_FRAME_SIZE];
};
typedef int (*hat_handler)(const struct hat_command *, char *, size_t, void *);
struct hat_parser {
	char line[HAT_LINE_MAX + 1];
	size_t used;
	bool discard;
};
/* Returns true when reply contains one complete response (including newline).
 */
bool hat_feed(struct hat_parser *, unsigned char, char *, size_t, hat_handler,
	      void *);
/* Discard a damaged/overflowed receive stream through its next newline. */
void hat_resync(struct hat_parser *);
/* Parse a command without its request ID. Returns NULL on success. */
const char *hat_parse_command(const char *, struct hat_command *);
/* Linux command syntax; staged upload commands are not exposed to clients. */
const char *hat_parse_client(const char *, struct hat_command *,
			     struct hat_animation *);
#endif
