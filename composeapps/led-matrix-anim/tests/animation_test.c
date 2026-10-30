/* SPDX-License-Identifier: Apache-2.0 */
#include "animation.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static unsigned draws;
static uint8_t shown;
static bool fail;
static int draw(const uint8_t *pixels)
{
	if (fail)
		return -EIO;
	++draws;
	shown = pixels[0];
	return 0;
}
static int command(struct hat_player *p, const char *s, int64_t now)
{
	struct hat_command cmd;
	assert(!hat_parse_command(s, &cmd));
	return hat_player_command(p, &cmd, now, draw);
}
static void upload(struct hat_player *p)
{
	assert(!command(p, "ANIM BEGIN 2", 0));
	assert(!command(p, "ANIM ADD 100 01000000000000000000000000000000", 0));
	assert(!command(p, "ANIM ADD 200 02000000000000000000000000000000", 0));
}
int main(void)
{
	struct hat_player p = {0};
	assert(command(&p, "ANIM PLAY 1", 0));
	assert(command(&p, "ANIM ADD 100 01000000000000000000000000000000", 0));
	upload(&p);
	assert(command(&p, "ANIM ADD 100 01000000000000000000000000000000", 0));
	assert(!command(&p, "ANIM PLAY 2", 1000));
	assert(draws == 1 && shown == 1);
	assert(!hat_player_tick(&p, 1099, draw) && draws == 1);
	assert(!hat_player_tick(&p, 1100, draw) && shown == 2);
	assert(!hat_player_tick(&p, 1299, draw) && draws == 2);
	assert(!hat_player_tick(&p, 1300, draw) && shown == 1);
	assert(!hat_player_tick(&p, 1400, draw) && shown == 2);
	assert(!hat_player_tick(&p, 1599, draw) && p.running);
	assert(!hat_player_tick(&p, 1600, draw) && !p.running && draws == 4);
	assert(!hat_player_tick(&p, 9000, draw) && shown == 2 && draws == 4);

	/* Infinite repetition, with old playback intact during an incomplete
	 * upload. */
	upload(&p);
	assert(!command(&p, "ANIM PLAY 0", 0));
	assert(!command(&p, "ANIM BEGIN 2", 1));
	assert(!command(&p, "ANIM ADD 20 04000000000000000000000000000000", 2));
	assert(command(&p, "ANIM PLAY 1", 3));
	assert(!hat_player_tick(&p, 100, draw) && shown == 2);
	assert(!hat_player_tick(&p, 300, draw) && shown == 1 && p.running);
	assert(
	    !command(&p, "ANIM ADD 20 08000000000000000000000000000000", 301));
	fail = true;
	assert(command(&p, "ANIM PLAY 1", 302));
	assert(p.running && shown == 1);
	fail = false;
	assert(!command(&p, "ANIM PLAY 1", 303) && shown == 4);
	assert(!hat_player_tick(&p, 323, draw) && shown == 8);
	assert(!hat_player_tick(&p, 343, draw) && !p.running);

	/* Stop cancels uploads and holds the frame; delayed wakeups never
	 * burst. */
	upload(&p);
	assert(!command(&p, "ANIM PLAY 0", 0));
	unsigned before = draws;
	assert(!hat_player_tick(&p, 10000, draw) && draws == before + 1);
	assert(p.deadline == 10200);
	assert(!command(&p, "ANIM STOP", 10001) && !p.running);
	assert(!hat_player_tick(&p, 20000, draw) && draws == before + 1);
	assert(command(&p, "ANIM PLAY 1", 20000));

	/* One frame still holds for its full duration; a driver error stops
	 * playback. */
	assert(!command(&p, "ANIM BEGIN 1", 0));
	assert(
	    !command(&p, "ANIM ADD 60000 01000000000000000000000000000000", 0));
	assert(!command(&p, "ANIM PLAY 1", 0));
	assert(!hat_player_tick(&p, 59999, draw) && p.running);
	assert(!hat_player_tick(&p, 60000, draw) && !p.running);
	upload(&p);
	assert(!command(&p, "ANIM PLAY 0", 0));
	fail = true;
	assert(hat_player_tick(&p, 100, draw) && !p.running);
	return 0;
}
