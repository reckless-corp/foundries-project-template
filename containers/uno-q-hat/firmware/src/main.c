/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

static const char hat[8][14] = {
	".............",
	"....##.##....",
	"...#######...",
	"...#######...",
	"...###.###...",
	".#####.#####.",
	"#############",
	"..#########..",
};

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	/* Driver uses LSB-first rows; pad the 13-pixel pitch to 16 bits. */
	uint8_t pixels[8][2] = {0};
	const struct display_buffer_descriptor desc = {
		.buf_size = sizeof(pixels), .width = 13, .height = 8, .pitch = 16,
	};

	if (!device_is_ready(display)) {
		return -ENODEV;
	}
	for (unsigned int y = 0; y < 8; ++y) {
		for (unsigned int x = 0; x < 13; ++x) {
			if (hat[y][x] == '#') {
				pixels[y][x / 8] |= BIT(x % 8);
			}
		}
	}
	int err = display_write(display, 0, 0, &desc, pixels);
	if (err != 0) {
		return err;
	}
	err = display_blanking_off(display);
	if (err != 0) {
		return err;
	}
	/* The display driver's timer keeps scanning without Linux involvement. */
	k_sleep(K_FOREVER);
	return 0;
}
