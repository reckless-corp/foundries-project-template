/* SPDX-License-Identifier: Apache-2.0 */
#include "animation.h"
#include <errno.h>
#include <string.h>

int hat_player_command(struct hat_player *p, const struct hat_command *cmd,
		       int64_t now, hat_draw draw)
{
	switch (cmd->operation) {
	case HAT_ANIM_BEGIN:
		if (!cmd->value || cmd->value > HAT_ANIM_MAX)
			return -EINVAL;
		p->expected = cmd->value;
		p->uploaded = 0;
		p->uploading = true;
		return 0;
	case HAT_ANIM_ADD: {
		if (!p->uploading || p->uploaded == p->expected)
			return -EINVAL;
		struct hat_step *step = &p->banks[p->active ^ 1][p->uploaded++];
		step->duration_ms = cmd->value;
		memcpy(step->pixels, cmd->pixels, HAT_FRAME_SIZE);
		return 0;
	}
	case HAT_ANIM_PLAY: {
		if (!p->uploading || p->uploaded != p->expected)
			return -EINVAL;
		struct hat_step *first = &p->banks[p->active ^ 1][0];
		int rc = draw(first->pixels);
		if (rc)
			return rc;
		p->active ^= 1;
		p->count = p->expected;
		p->index = 0;
		p->remaining = cmd->value;
		p->deadline = now + first->duration_ms;
		p->running = true;
		p->uploading = false;
		return 0;
	}
	case HAT_ANIM_STOP:
		p->running = false;
		p->uploading = false;
		return 0;
	default:
		return -EINVAL;
	}
}

int hat_player_tick(struct hat_player *p, int64_t now, hat_draw draw)
{
	if (!p->running || now < p->deadline)
		return 0;
	size_t next = p->index + 1;
	if (next == p->count) {
		if (p->remaining == 1) {
			p->running = false;
			return 0;
		}
		if (p->remaining)
			--p->remaining;
		next = 0;
	}
	struct hat_step *step = &p->banks[p->active][next];
	int rc = draw(step->pixels);
	if (rc) {
		p->running = false;
		return rc;
	}
	p->index = next;
	/* Never burst through frames after a delayed wakeup. */
	p->deadline = now + step->duration_ms;
	return 0;
}
