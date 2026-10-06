/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HAT_PROTOCOL_H
#define HAT_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HAT_LINE_MAX 128
#define HAT_FRAME_SIZE 16
enum hat_operation { HAT_HELLO, HAT_PING, HAT_SHOW, HAT_DISPLAY, HAT_FRAME };
struct hat_command {
	enum hat_operation operation;
	bool visible;
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
#endif
