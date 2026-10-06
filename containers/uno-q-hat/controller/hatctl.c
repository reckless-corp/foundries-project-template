/* SPDX-License-Identifier: Apache-2.0 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "protocol.h"

static volatile sig_atomic_t stopping;
static uint32_t sequence;
static int serial_fd = -1;
static uint32_t boot_id;
static bool exclusive;
static char desired_frame[40] = "SHOW HAT";
static char desired_display[16] = "DISPLAY ON";
static bool online;

static const char *setting(const char *name, const char *fallback)
{
	const char *value = getenv(name);
	return value && *value ? value : fallback;
}
static int64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void stop(int sig)
{
	(void)sig;
	stopping = 1;
}
static int wait_fd(int fd, short events, int64_t deadline)
{
	while (!stopping) {
		int64_t left = deadline - now_ms();
		if (left <= 0)
			return -1;
		struct pollfd p = {.fd = fd, .events = events};
		int n = poll(&p, 1, (int)left);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		if (p.revents & events)
			return 0;
		return -1;
	}
	return -1;
}
static int send_all(int fd, const char *s, int64_t deadline)
{
	size_t left = strlen(s);
	while (left && !stopping) {
		if (wait_fd(fd, POLLOUT, deadline))
			return -1;
		ssize_t n = write(fd, s, left);
		if (n < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (n <= 0)
			return -1;
		s += n;
		left -= (size_t)n;
	}
	return left ? -1 : 0;
}
/* Consume an oversized/invalid line completely; never execute a suffix of it.
 */
static int read_line(int fd, char *line, size_t size, int64_t deadline)
{
	size_t used = 0;
	bool bad = size == 0;
	for (;;) {
		if (wait_fd(fd, POLLIN, deadline))
			return -1;
		unsigned char c;
		ssize_t n = read(fd, &c, 1);
		if (n < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (n <= 0)
			return -1;
		if (c == '\n') {
			if (size)
				line[used] = 0;
			return bad ? -2 : 0;
		}
		/* Keep one byte free for the terminating NUL. */
		if (c < 32 || c > 126 || used + 1 >= size)
			bad = true;
		else if (!bad)
			line[used++] = (char)c;
	}
}
static void disconnect_mcu(void)
{
	if (serial_fd >= 0) {
		if (exclusive)
			ioctl(serial_fd, TIOCNXCL);
		exclusive = false;
		close(serial_fd);
		serial_fd = -1;
	}
	if (online)
		fprintf(stderr,
			"MCU unavailable; reconnecting without reflashing\n");
	online = false;
}
static int open_uart(void)
{
	serial_fd = open(setting("MCU_UART", "/dev/ttyHS1"),
			 O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (serial_fd < 0)
		return -1;
	struct termios tty;
	if (flock(serial_fd, LOCK_EX | LOCK_NB) || ioctl(serial_fd, TIOCEXCL))
		goto fail;
	exclusive = true;
	if (tcgetattr(serial_fd, &tty))
		goto fail;
	cfmakeraw(&tty);
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);
	tty.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
	tty.c_cflag |= CLOCAL | CREAD;
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;
	if (tcsetattr(serial_fd, TCSANOW, &tty) ||
	    tcflush(serial_fd, TCIOFLUSH) ||
	    send_all(serial_fd, "\n", now_ms() + 750))
		goto fail;
	return 0;
fail:
	disconnect_mcu();
	return -1;
}
/* Return 0 for OK, 1 for a remote ERR, -1 for transport/protocol failure. */
static int request(const char *command, char *reply, size_t size)
{
	char wire[HAT_LINE_MAX + 1], prefix[24];
	if (++sequence == 0)
		++sequence;
	snprintf(wire, sizeof(wire), "%" PRIu32 " %s\n", sequence, command);
	snprintf(prefix, sizeof(prefix), "%" PRIu32 " ", sequence);
	int64_t deadline = now_ms() + 750;
	if (send_all(serial_fd, wire, deadline))
		return -1;
	for (;;) {
		int rc = read_line(serial_fd, wire, sizeof(wire), deadline);
		if (rc == -1)
			return -1;
		if (rc || strncmp(wire, prefix, strlen(prefix)))
			continue;
		const char *body = wire + strlen(prefix);
		if (strcmp(body, "OK") && strncmp(body, "OK ", 3) &&
		    strncmp(body, "ERR ", 4))
			return -1;
		snprintf(reply, size, "%s", body);
		return !strncmp(body, "ERR ", 4);
	}
}
static bool get_boot(const char *reply, uint32_t *value)
{
	const char *s = strstr(reply, " boot=");
	if (!s)
		return false;
	s += 6;
	if (strlen(s) < 8 || strspn(s, "0123456789abcdefABCDEF") != 8 ||
	    (s[8] && s[8] != ' '))
		return false;
	*value = (uint32_t)strtoul(s, NULL, 16);
	return true;
}
static int handshake(void)
{
	char reply[HAT_LINE_MAX + 1];
	if (request("HELLO", reply, sizeof(reply)) ||
	    !strstr(reply, " proto=1 ") ||
	    !strstr(reply, " width=13 height=8 ") || !get_boot(reply, &boot_id))
		return -1;
	fprintf(stderr, "MCU %s\n", reply);
	if (request(desired_frame, reply, sizeof(reply)) ||
	    request(desired_display, reply, sizeof(reply)))
		return -1;
	online = true;
	return 0;
}
static int ensure_mcu(void)
{
	if (serial_fd < 0) {
		if (open_uart() || handshake())
			goto fail;
		return 0;
	}
	char reply[HAT_LINE_MAX + 1];
	uint32_t current_boot;
	if (request("PING", reply, sizeof(reply)) ||
	    !get_boot(reply, &current_boot))
		goto fail;
	if (current_boot != boot_id && handshake())
		goto fail;
	return 0;
fail:
	disconnect_mcu();
	return -1;
}
static int address(struct sockaddr_un *addr)
{
	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	const char *path = setting("MCU_SOCKET", "/run/uno-q-hat/control.sock");
	if (strlen(path) >= sizeof(addr->sun_path))
		return -1;
	strcpy(addr->sun_path, path);
	return 0;
}
static int daemon_main(void)
{
	struct sockaddr_un addr;
	if (address(&addr))
		return 1;
	/* A separate lock protects against unlinking another live daemon's
	 * socket. */
	char lock_path[sizeof(addr.sun_path) + 6];
	snprintf(lock_path, sizeof(lock_path), "%s.lock", addr.sun_path);
	int lock = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
		fprintf(stderr, "Cannot lock socket directory (missing "
				"directory or another controller)\n");
		if (lock >= 0)
			close(lock);
		return 1;
	}
	int server =
	    socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	unlink(addr.sun_path);
	if (server < 0 ||
	    bind(server, (struct sockaddr *)&addr, sizeof(addr)) ||
	    chmod(addr.sun_path, 0660) || listen(server, 8)) {
		perror("control socket");
		if (server >= 0)
			close(server);
		close(lock);
		return 1;
	}
	fprintf(stderr, "Controller listening on %s, UART %s\n", addr.sun_path,
		setting("MCU_UART", "/dev/ttyHS1"));
	int64_t next_ping = 0;
	while (!stopping) {
		if (now_ms() >= next_ping) {
			ensure_mcu();
			next_ping = now_ms() + 2000;
		}
		if (wait_fd(server, POLLIN, next_ping))
			continue;
		int client = accept(server, NULL, NULL);
		if (client < 0)
			continue;
		fcntl(client, F_SETFL, O_NONBLOCK);
		char command[HAT_LINE_MAX + 1], reply[HAT_LINE_MAX + 1];
		int rc =
		    read_line(client, command, sizeof(command), now_ms() + 500);
		struct hat_command parsed;
		const char *error =
		    rc ? "BAD_LINE" : hat_parse_command(command, &parsed);
		if (error)
			snprintf(reply, sizeof(reply), "ERR %s", error);
		else if (ensure_mcu() ||
			 (rc = request(command, reply, sizeof(reply))) < 0) {
			disconnect_mcu();
			strcpy(reply, "ERR UNAVAILABLE");
		} else if (rc == 0) {
			if (parsed.operation == HAT_SHOW ||
			    parsed.operation == HAT_FRAME)
				strcpy(
				    desired_frame,
				    command); /* Validated FRAME/SHOW length. */
			if (parsed.operation == HAT_DISPLAY)
				strcpy(desired_display,
				       command); /* Validated DISPLAY length. */
		}
		send_all(client, reply, now_ms() + 500);
		send_all(client, "\n", now_ms() + 500);
		close(client);
	}
	disconnect_mcu();
	close(server);
	unlink(addr.sun_path);
	close(lock);
	return 0;
}
int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, stop);
	signal(SIGINT, stop);
	if (argc == 2 && !strcmp(argv[1], "daemon"))
		return daemon_main();
	char command[HAT_LINE_MAX + 1] = "";
	for (int i = 1; i < argc; ++i) {
		if (strlen(command) + strlen(argv[i]) + 2 > sizeof(command))
			return 2;
		if (i > 1)
			strcat(command, " ");
		strcat(command, argv[i]);
	}
	struct hat_command parsed;
	const char *error = hat_parse_command(command, &parsed);
	if (argc < 2 || error) {
		fprintf(stderr,
			"Usage: uno-q-hatctl {HELLO|PING|SHOW HAT|DISPLAY "
			"ON|DISPLAY OFF|FRAME hex|daemon}\n");
		return 2;
	}
	strcat(command, "\n");
	struct sockaddr_un addr;
	if (address(&addr))
		return 1;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
		perror("connect controller");
		if (fd >= 0)
			close(fd);
		return 1;
	}
	char reply[HAT_LINE_MAX + 1];
	int64_t deadline = now_ms() + 6000;
	if (send_all(fd, command, deadline) ||
	    read_line(fd, reply, sizeof(reply), deadline)) {
		fprintf(stderr, "Controller timed out or disconnected\n");
		close(fd);
		return 1;
	}
	close(fd);
	puts(reply);
	return strcmp(reply, "OK") && strncmp(reply, "OK ", 3) ? 1 : 0;
}
