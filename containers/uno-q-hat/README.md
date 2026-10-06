# UNO Q cowboy hat

A standalone Zephyr application draws a 13×8 cowboy hat on the UNO Q's STM32
LED matrix. An ARM64 container installs it through the Qualcomm processor's
GPIO/SWD connection. Once started, the MCU drives the LEDs independently.
After installation, a small Linux controller communicates with the MCU over its
internal UART. Docker restarts the service at boot to initialize the MCU boot pin,
reset it into the application, and reconnect the controller.

This replaces Arduino's MCU sketch-loader firmware. It does not require an
Arduino Linux distribution, arduino-cli, an external programmer, or a Yocto
rebuild. It does not implement Arduino Bridge/RPC or MCUboot updates.

## Build

From the repository root, on an x86-64 Docker host:

```sh
docker buildx build --platform linux/arm64 --load \
  -t ghcr.io/reckless-corp/uno-q-hat:dev containers/uno-q-hat
python3 -B -m unittest discover -s containers/uno-q-hat/tests -v
docker compose -f containers/uno-q-hat/docker-compose.yml config --quiet
```

The native build stages cross-compile Cortex-M firmware, ARM64 OpenOCD, and the
ARM64 controller; QEMU is unnecessary. Host tests need Python 3, a C compiler
(`cc`), and permission to create local Unix sockets and pseudo-terminals. The Dockerfile pins the Debian base digest, Zephyr and
OpenOCD commits, west, and the SDK archives/checksums. Debian packages and Python
transitive dependencies still follow their package indexes, so this is not a
bit-for-bit reproducible build. A build-time ELF check rejects an incorrect
architecture, flash placement or reset vector.

The firmware bitmap is in `firmware/src/main.c`. Rows have 13 pixels; `#` means
lit. The pinned matrix driver expects LSB-first bits and a 16-bit padded pitch.
Its timer maintains the display while the application sleeps.

## Local installation over ADB

The board must run Docker, expose `/dev/gpiochip1` and the internal MCU UART
(`/dev/ttyHS1` on the tested Yocto image), and allow root ADB (or use
equivalent SSH commands). Replace `3ac9982a` with the output of `adb devices`.
Copy the image and compose file:

```sh
docker save -o /tmp/uno-q-hat.tar ghcr.io/reckless-corp/uno-q-hat:dev
adb -s 3ac9982a push /tmp/uno-q-hat.tar /var/tmp/uno-q-hat.tar
adb -s 3ac9982a shell docker load -i /var/tmp/uno-q-hat.tar
adb -s 3ac9982a shell mkdir -p /var/tmp/uno-q-hat
adb -s 3ac9982a push containers/uno-q-hat/docker-compose.yml /var/tmp/uno-q-hat/
```

For local testing, tag the loaded image with the compose file's untagged name:

```sh
adb -s 3ac9982a shell docker tag ghcr.io/reckless-corp/uno-q-hat:dev ghcr.io/reckless-corp/uno-q-hat:latest
```

Run a backup first. All commands below run on the board in
`/var/tmp/uno-q-hat`, using the same compose project name so they share state:

```sh
docker compose -p uno-q-hat run --rm --no-deps hat backup
docker compose -p uno-q-hat up -d --no-build --pull never --wait --wait-timeout 360
docker compose -p uno-q-hat logs hat
docker compose -p uno-q-hat run --rm --no-deps hat verify
```

Installation also creates a backup automatically if none exists. It validates
the board model and GPIO controller, rejects occupied SWD/reset lines, and uses
a lock in the state volume to serialize operations. `/dev/gpiochip1` and the
internal UART are passed through. The container has no network, added capabilities,
host Docker socket, or privileged mode.

Arduino's board configuration assigns SWDIO=25, SWCLK=26 and NRST=38 on
`gpiochip1` (`500000.pinctrl`, 127 lines). Do not reuse this mapping on another
board. The device-tree directory is mounted read-only for model verification.
Arduino's router also initializes GPIO37 low to select MCU application boot.
The installer holds that pin low during programming and reset, then checks that
the program counter enters application flash. Without this step, flash
verification can succeed while the MCU stays in ST's boot ROM. The board needs
this initialization after power loss; firmware persistence alone is insufficient.

## UART control

Run commands on the board against the running service (from the deployment's
compose directory):

```sh
docker compose -p uno-q-hat exec -T hat uno-q-hatctl HELLO
docker compose -p uno-q-hat exec -T hat uno-q-hatctl PING
docker compose -p uno-q-hat exec -T hat uno-q-hatctl DISPLAY OFF
docker compose -p uno-q-hat exec -T hat uno-q-hatctl DISPLAY ON
docker compose -p uno-q-hat exec -T hat uno-q-hatctl SHOW HAT
# Light all 104 pixels:
docker compose -p uno-q-hat exec -T hat uno-q-hatctl FRAME ff1fff1fff1fff1fff1fff1fff1fff1f
```

To blink a plus sign on for 500 ms and off for 500 ms, upload the complete
animation in one command:

```sh
docker compose -p uno-q-hat exec -T hat uno-q-hatctl ANIM 0 \
  500:000040004000f8034000400000000000 \
  500:00000000000000000000000000000000
```

The syntax is `ANIM <repeat-count> <milliseconds>:<32-hex-digits> [...]`.
Use `0` for infinite repetition or `1`–`4294967295` for that many complete cycles.
An animation accepts 1–64 frames, each held for 20–60,000 ms. The last frame is
held for its duration before finite playback ends, then remains displayed.
`ANIM STOP` stops playback and holds the current image. A successful `FRAME` or
`SHOW HAT` also stops playback. `DISPLAY OFF` hides the animation without pausing
it; use `DISPLAY ON` to make it visible again.

The whole command is validated before uploading. The MCU keeps playing its
previous animation until the new upload is complete and its first frame is
successfully written. Playback runs on the MCU, independently of the CLI process.
A delayed display update holds the next frame for its full duration rather than
rushing through missed frames. If a display write fails during playback, playback
stops; the initial `OK` acknowledges starting the animation, not future updates.

The controller retains the last acknowledged animation in memory. After a UART
reconnect or MCU reset it uploads and restarts that animation from the beginning,
including the full repeat count for a finite animation. After `ANIM STOP`, it
restores the actual stopped frame instead. Restarting the controller itself
returns to `SHOW HAT` and `DISPLAY ON`; animations are not persisted to disk.
When firmware lacks the `anim=64` HELLO capability, the controller returns
`ERR UNSUPPORTED` for a new animation upload. Rebuild and install both firmware and controller to use it.

`SHOW HAT` and `FRAME` update the image without changing visibility. Commands exit
zero only for `OK`; MCU errors, missing controller, and timeouts exit nonzero.
`HELLO` reports protocol version, an application source/configuration fingerprint,
matrix dimensions, and the MCU's random 32-bit boot identifier. `PING` reports that
identifier and uptime. The boot identifier detects MCU restarts; it is not a
security token or a persistent unique identifier.

The controller alone owns the UART. On an Arduino stock image, stop the Arduino
router before starting this application. Configure `MCU_UART_DEVICE` in the
Compose environment if the host UART has a different path. Compose maps that
path to `/dev/uno-q-mcu` and sets `MCU_UART` for the controller. No additional
wiring is needed. Firmware uses `lpuart1` at 115200 8N1 without hardware flow
control; the header UART and the SWD flashing path are independent.

The controller checks the MCU every two seconds. After reconnecting or detecting
a changed boot identifier, it negotiates protocol version 1 and reapplies the
last acknowledged image and visibility. These desired settings live in controller
memory: restarting the controller restores the built-in hat with the display on.
If Linux stops, the MCU retains its static image or continues animation playback;
resetting the MCU starts with the hat until Linux restores the desired settings.
An interrupted request may have executed even if its response was lost. Retrying
a client `ANIM` command restarts the animation with its full repeat count.

### Local socket API

The `control-socket` volume contains `/run/uno-q-hat/control.sock`, a Unix stream
socket with mode `0660`, owned by the controller's UID/GID (root in this image).
A second container can mount the same volume and connect with a compatible
UID/GID. It needs neither UART/GPIO access nor network access. Share the directory,
not an individual socket inode, so reconnecting works when the socket is recreated.
The volume name is scoped to the Compose project; separate projects can reference
that existing volume explicitly.

Send one ASCII command followed by LF per connection, without a request ID:

```text
DISPLAY OFF\n
```

Read until LF, then close. A successful response is `OK` (optionally followed by
fields); errors are `ERR <code>`. Socket reads may split or combine data: do not
assume one `recv()` returns a complete line. The daemon serializes clients and
assigns UART request IDs. `MCU_SOCKET` overrides the default socket path; the
parent directory must exist when invoking the daemon directly. The controller
has no TCP listener and works with `network_mode: none`.

### Wire protocol v1

Each UART request is `<id> <command>\n`. IDs are decimal integers from 1 through
4294967295 without leading zeros. A response is `<id> OK[ <fields>]\n` or
`<id> ERR <code>\n`. ID 0 is reserved for errors whose request ID cannot be
recovered. The MCU accepts CRLF as well as LF; the local socket accepts LF only.
Commands and spacing are case-sensitive.

| Command | Result / effect |
| --- | --- |
| `HELLO` | `proto=1 firmware=<fingerprint> width=13 height=8 anim=64 boot=<8 hex digits>` |
| `PING` | `boot=<8 hex digits> uptime_ms=<milliseconds>` |
| `SHOW HAT` | Replace image with the built-in hat; preserve visibility |
| `DISPLAY ON` / `DISPLAY OFF` | Set visibility; preserve image |
| `FRAME <32 hex digits>` | Replace the full monochrome image; preserve visibility |
| `ANIM BEGIN <frames>` | Begin staging 1–64 frames without changing playback |
| `ANIM ADD <ms> <32 hex digits>` | Append a staged frame with a 20–60,000 ms hold |
| `ANIM PLAY <repeat-count>` | Commit a complete upload and start playback; 0 loops forever |
| `ANIM STOP` | Stop playback and return `frame=<32 hex digits>` for recovery |

The staged `BEGIN`/`ADD`/`PLAY` commands above are internal UART commands. The
controller serializes uploads, translating the single client `ANIM` command into
these short requests and acknowledging the client only after `PLAY` succeeds.
Each new `BEGIN` replaces any incomplete staging upload. Incomplete uploads and
failed commits leave current playback intact; invalid staging state returns
`ERR IO`. The MCU uses two fixed frame buffers and no dynamic allocation.
`HELLO` and `PING` remain responsive during playback.

Local CLI/socket commands can contain up to 4096 characters; serial requests keep
the 128-character limit below. The CLI allows up to 120 seconds for a response,
covering a maximum-sized recovery upload followed by a new upload. Each serial
request retains its 750 ms response deadline.

A frame contains 16 bytes: two bytes per row, top to bottom. In each row, the first
byte contains columns 0–7, least significant bit first; the second contains columns
8–12 in bits 0–4. Bits 5–7 of the second byte must be zero. Both hex cases are
accepted. Invalid frames are rejected before touching the display. `OK` for a
display mutation means its display-driver call succeeded; it does not promise a
whole physical refresh has completed. Staging commands only acknowledge storage.

Lines are limited to 128 bytes excluding LF (including optional CR). The MCU
has a 256-byte receive ring; interrupt handling only buffers input. The main
thread parses and executes commands and replies. Invalid/non-ASCII input or line
and receive-buffer overflow triggers discarding through the next LF. Errors are
`BAD_LINE`, `BAD_ID`, `BAD_ARGUMENT`, `UNKNOWN_COMMAND`, and `IO`; the controller
adds `UNAVAILABLE` for connection, negotiation, or response failures,
`UNSUPPORTED` for missing animation capability, and `BAD_REPLY` for a malformed
stop response. No debug
logs share this UART. There is no checksum, authentication, or bulk-transfer mode.

The controller permits one outstanding UART request and waits at most 750 ms for
its response. Unrelated/stale IDs are ignored within that deadline. Reconnecting
flushes local serial queues and sends an LF to terminate any partial MCU request.
Malformed or missing replies cause reconnection; they never trigger reflashing.

## Backup and recovery

The `firmware-backup` volume contains `original.bin` (all 2 MiB), its SHA-256,
and `backup.log` with option-register readings. Backups are verified against MCU
memory before they are marked complete. The first completed backup is preserved
across later upgrades; corrupted backups block installation. Option bytes are
never modified by this application.
The volume has the fixed Docker name `uno-q-hat_firmware-backup`, so local
projects and the published `uno-q-hat-app` share the same original backup and lock.
Stop the local test project before enabling the published app: only one deployed
service should own the MCU's desired firmware version.

Export the backup off-board before experimental firmware changes:

```sh
docker compose -p uno-q-hat run --rm --no-deps --entrypoint tar hat \
  -C /state -cf - original.bin original.sha256 backup.log > original-mcu.tar
```

Keep that archive somewhere durable; removing the Docker volume loses the
on-board recovery copy. To restore the original firmware from the volume:

```sh
docker compose -p uno-q-hat stop hat
docker compose -p uno-q-hat run --rm --no-deps hat restore
```

Stopping or uninstalling the composeapp does **not** stop or undo MCU firmware.
To downgrade, run the previous container image's installer, then verify it.
If an installation fails or loses power, rerun the installer or restore the
backup. Direct SWD writes are not atomic; there is no automatic MCU rollback.
A failure can leave the MCU halted until a successful retry or reset.

## CI and status

The repository workflow publishes `uno-q-hat` and `uno-q-hat-app` for ARM64 only,
pins the container digest in the composeapp, and includes it only with UNO Q
update artifacts. The existing Matrix app remains available for both machines.

The one-shot `install`, `verify`, `backup`, and `restore` commands exit zero on
success and nonzero on failure. Compose runs `serve`: initialize once, then run
`uno-q-hatctl daemon`. Health runs a live `uno-q-hatctl PING` through the controller
and UART, including protocol negotiation and state recovery when necessary.
Installation failure leaves the service idle and unhealthy without retrying flash;
inspect logs and explicitly restart it after correcting the problem. UART failures
are retried without reflashing. Allow five minutes for a first full backup.
`init: true` forwards shutdown signals to the controller.
`restart: unless-stopped` runs initialization again after Docker starts.

Stopping the service leaves the current display running but disables automatic
initialization at the next boot. The firmware is compared on each start and
written only when different. The hardware acceptance checks are a visible hat,
unchanged-image startup without writes, cold-boot startup, and explicit downgrade.

For manually invoked one-shot containers, check the command exit status. The UART
health check applies to the long-running `serve` service; one-shot maintenance
commands do not start a controller.

## Hardware validation

The original SWD installer and static display were validated on the project's
UNO Q Yocto image with Docker 29.7.2:

- Full 2 MiB flash backup and verification; option bytes left unchanged.
- Firmware write, read-back verification, unchanged-image startup without writes,
  and installation of a second bitmap followed by downgrade.
- Application execution confirmed at a flash address after reset with GPIO37 low.
- Automatic startup after a board reboot: healthy, executing Zephyr, no flash write.
- Local-registry composeapp publication, offline installation with composectl,
  and healthy service status. A container with no board access stays unhealthy
  without repeatedly attempting installation.

The UART implementation builds for the pinned Zephyr target and ARM64 container.
Host tests cover the installer, protocol validation, stream recovery, animation
timing and atomic replacement, and the real controller communicating with a
simulated MCU over a pseudo-terminal (including 64-frame uploads and recovery).
The board's `/dev/ttyHS1` was verified read-only; the UART firmware has not yet been
flashed or hardware-tested. Validate display commands, MCU-reset recovery,
controller restart, and a physical power-cycle before deploying it broadly.

The animation-enabled UART firmware uses 30,948 bytes of flash and 9,752 bytes of RAM. Hardware backup
and logs from development are kept in the ignored `.local/` directory; they are
excluded from both Docker and composeapp publication. Visual appearance and a
physical power-cycle still need confirmation at the board.

The development deployment uses `/var/lib/uno-q-hat/projects/uno-q-hat/` on the
board. Its pinned compose file can be used with `docker compose -p uno-q-hat -f
/var/lib/uno-q-hat/projects/uno-q-hat/docker-compose.yml` for logs or maintenance.

Sources: [Zephyr UNO Q](https://docs.zephyrproject.org/latest/boards/arduino/uno_q/doc/index.html),
[Arduino SWD configuration](https://github.com/arduino/meta-arduino/tree/master/meta-arduino-qcom/recipes-devtools/openocd/files/imola),
[Foundries one-shot status](https://docs.foundries.io/95/tutorials/compose-app/compose-app-checking-app-state.html).
