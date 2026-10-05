/* SPDX-License-Identifier: Apache-2.0 */
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd = open("/dev/gpiochip1", O_RDONLY | O_CLOEXEC);
	struct gpiochip_info chip = {0};
	if (fd < 0 || ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chip) < 0) {
		perror("Cannot inspect /dev/gpiochip1");
		return 1;
	}
	if (strcmp(chip.label, "500000.pinctrl") || chip.lines != 127) {
		fprintf(stderr, "Unexpected GPIO controller: %s (%u lines)\n", chip.label, chip.lines);
		return 1;
	}
	const unsigned int pins[] = {25, 26, 37, 38};
	for (unsigned int i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
		struct gpio_v2_line_info line = {.offset = pins[i]};
		if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &line) < 0) {
			perror("Cannot inspect SWD line");
			return 1;
		}
		if (line.flags & GPIO_V2_LINE_FLAG_USED) {
			fprintf(stderr, "GPIO %u is owned by %s\n", pins[i], line.consumer);
			return 1;
		}
	}
	if (argc > 1) {
		/* Arduino's 10-imola.conf drives GPIO37 low before MCU startup.
		 * Hold it for the entire OpenOCD operation, including reset.
		 */
		struct gpio_v2_line_request boot = {
			.offsets = {37}, .consumer = "uno-q-hat-boot",
			.config = {.flags = GPIO_V2_LINE_FLAG_OUTPUT}, .num_lines = 1,
		};
		if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &boot) < 0 ||
		    fcntl(boot.fd, F_SETFD, 0) < 0) {
			perror("Cannot hold MCU boot pin low");
			return 1;
		}
		close(fd);
		execvp(argv[1], &argv[1]);
		perror("Cannot execute flasher");
		return 1;
	}
	close(fd);
	return 0;
}
