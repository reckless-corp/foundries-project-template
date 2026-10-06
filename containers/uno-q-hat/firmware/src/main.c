/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <inttypes.h>
#include <stdio.h>
#include "protocol.h"

static const char hat[8][14] = {
    ".............", "....##.##....", "...#######...", "...#######...",
    "...###.###...", ".#####.#####.", "#############", "..#########..",
};
static const struct device *const display =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(lpuart1));
RING_BUF_DECLARE(rx, 256);
K_SEM_DEFINE(rx_ready, 0, 1);
static atomic_t overflow;
static uint32_t boot_id;

static void receive(const struct device *dev, void *context)
{
	ARG_UNUSED(context);
	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev)) {
		uint8_t bytes[16];
		int n = uart_fifo_read(dev, bytes, sizeof(bytes));
		if (n <= 0)
			break;
		if (ring_buf_put(&rx, bytes, n) != (uint32_t)n) {
			atomic_set(&overflow, 1);
			uart_irq_rx_disable(dev);
			break;
		}
	}
	k_sem_give(&rx_ready);
}

static int write_frame(const uint8_t *pixels)
{
	const struct display_buffer_descriptor desc = {
	    .buf_size = HAT_FRAME_SIZE,
	    .width = 13,
	    .height = 8,
	    .pitch = 16,
	};
	return display_write(display, 0, 0, &desc, pixels);
}

static int show_hat(void)
{
	uint8_t pixels[HAT_FRAME_SIZE] = {0};
	for (unsigned y = 0; y < 8; ++y)
		for (unsigned x = 0; x < 13; ++x)
			if (hat[y][x] == '#')
				pixels[y * 2 + x / 8] |= BIT(x % 8);
	return write_frame(pixels);
}

static int execute(const struct hat_command *cmd, char *result, size_t size,
		   void *context)
{
	ARG_UNUSED(context);
	switch (cmd->operation) {
	case HAT_HELLO:
		snprintf(result, size,
			 "proto=1 firmware=" HAT_FIRMWARE_VERSION
			 " width=13 height=8 boot=%08" PRIx32,
			 boot_id);
		return 0;
	case HAT_PING:
		snprintf(result, size, "boot=%08" PRIx32 " uptime_ms=%" PRId64,
			 boot_id, k_uptime_get());
		return 0;
	case HAT_SHOW:
		return show_hat();
	case HAT_FRAME:
		return write_frame(cmd->pixels);
	case HAT_DISPLAY:
		return cmd->visible ? display_blanking_off(display)
				    : display_blanking_on(display);
	}
	return -EINVAL;
}

int main(void)
{
	if (!device_is_ready(display) || !device_is_ready(uart))
		return -ENODEV;
	int err = show_hat();
	if (!err)
		err = display_blanking_off(display);
	if (err)
		return err;
	boot_id = sys_rand32_get();
	err = uart_irq_callback_user_data_set(uart, receive, NULL);
	if (err)
		return err;
	uart_irq_rx_enable(uart);
	struct hat_parser parser = {0};
	char reply[HAT_LINE_MAX + 1];
	for (;;) {
		k_sem_take(&rx_ready, K_FOREVER);
		uint8_t c;
		for (;;) {
			unsigned key = irq_lock();
			if (atomic_set(&overflow, 0)) {
				ring_buf_reset(&rx);
				hat_resync(&parser);
				uart_irq_rx_enable(uart);
			}
			uint32_t n = ring_buf_get(&rx, &c, 1);
			irq_unlock(key);
			if (!n)
				break;
			if (hat_feed(&parser, c, reply, sizeof(reply), execute,
				     NULL))
				for (const char *s = reply; *s; ++s)
					uart_poll_out(uart, *s);
		}
	}
}
