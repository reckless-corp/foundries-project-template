/* SPDX-License-Identifier: Apache-2.0 */
#ifndef HAT_ANIMATION_H
#define HAT_ANIMATION_H
#include "protocol.h"

struct hat_player {
	struct hat_step banks[2][HAT_ANIM_MAX];
	unsigned active;
	size_t count, index, expected, uploaded;
	uint32_t remaining;
	bool uploading, running;
	int64_t deadline;
};
typedef int (*hat_draw)(const uint8_t *);
int hat_player_command(struct hat_player *, const struct hat_command *, int64_t,
		       hat_draw);
int hat_player_tick(struct hat_player *, int64_t, hat_draw);
#endif
