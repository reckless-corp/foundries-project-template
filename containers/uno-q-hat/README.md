# UNO Q cowboy hat

A standalone Zephyr application draws a 13×8 cowboy hat on the UNO Q's STM32
LED matrix. An ARM64 container installs it through the Qualcomm processor's
GPIO/SWD connection. Once started, the MCU drives the LEDs independently.
The compose service stays idle after installation so Docker restarts it at
boot to initialize the MCU boot pin and reset it into the application.

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

The native build stages cross-compile both Cortex-M firmware and ARM64 OpenOCD;
QEMU is unnecessary. The Dockerfile pins the Debian base digest, Zephyr and
OpenOCD commits, west, and the SDK archives/checksums. Debian packages and Python
transitive dependencies still follow their package indexes, so this is not a
bit-for-bit reproducible build. A build-time ELF check rejects an incorrect
architecture, flash placement or reset vector.

The firmware bitmap is in `firmware/src/main.c`. Rows have 13 pixels; `#` means
lit. The pinned matrix driver expects LSB-first bits and a 16-bit padded pitch.
Its timer maintains the display while the application sleeps.

## Local installation over ADB

The board must run Docker, expose `/dev/gpiochip1`, and allow root ADB (or use
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
a lock in the state volume to serialize operations. Only `/dev/gpiochip1` is
passed through. The container has no network, added capabilities, host Docker
socket, or privileged mode.

Arduino's board configuration assigns SWDIO=25, SWCLK=26 and NRST=38 on
`gpiochip1` (`500000.pinctrl`, 127 lines). Do not reuse this mapping on another
board. The device-tree directory is mounted read-only for model verification.
Arduino's router also initializes GPIO37 low to select MCU application boot.
The installer holds that pin low during programming and reset, then checks that
the program counter enters application flash. Without this step, flash
verification can succeed while the MCU stays in ST's boot ROM. The board needs
this initialization after power loss; firmware persistence alone is insufficient.

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
success and nonzero on failure. Compose runs `serve`: initialize once and stay
idle. It becomes healthy only after successful installation and an application
execution check. On failure it stays unhealthy without retrying flash; inspect
logs and explicitly restart it after correcting the problem. Allow five minutes
for a first full backup. `init: true` forwards shutdown signals to the idle
process. `restart: unless-stopped` runs initialization again after Docker starts.

Health indicates successful initialization, not continuous MCU liveness.
Stopping the service leaves the current display running but disables automatic
initialization at the next boot. The firmware is compared on each start and
written only when different. The hardware acceptance checks are a visible hat,
unchanged-image startup without writes, cold-boot startup, and explicit downgrade.

For manually invoked one-shot containers, the tested composectl reports `not running`
in its summary and `healthy` in `ps --format json` service health. Check the
service exit code/health rather than treating that summary as a failure.

## Hardware validation

Validated on the project's UNO Q Yocto image with Docker 29.7.2:

- Full 2 MiB flash backup and verification; option bytes left unchanged.
- Firmware write, read-back verification, unchanged-image startup without writes,
  and installation of a second bitmap followed by downgrade.
- Application execution confirmed at a flash address after reset with GPIO37 low.
- Automatic startup after a board reboot: healthy, executing Zephyr, no flash write.
- Local-registry composeapp publication, offline installation with composectl,
  and healthy service status. A container with no board access stays unhealthy
  without repeatedly attempting installation.
- Sixteen host tests cover backup preservation, failed operations, readiness,
  compare-before-write, verification failure, and detecting execution in boot ROM.

The firmware uses 22,328 bytes of flash and 4,464 bytes of RAM. Hardware backup
and logs from development are kept in the ignored `.local/` directory; they are
excluded from both Docker and composeapp publication. Visual appearance and a
physical power-cycle still need confirmation at the board.

The development deployment uses `/var/lib/uno-q-hat/projects/uno-q-hat/` on the
board. Its pinned compose file can be used with `docker compose -p uno-q-hat -f
/var/lib/uno-q-hat/projects/uno-q-hat/docker-compose.yml` for logs or maintenance.

Sources: [Zephyr UNO Q](https://docs.zephyrproject.org/latest/boards/arduino/uno_q/doc/index.html),
[Arduino SWD configuration](https://github.com/arduino/meta-arduino/tree/master/meta-arduino-qcom/recipes-devtools/openocd/files/imola),
[Foundries one-shot status](https://docs.foundries.io/95/tutorials/compose-app/compose-app-checking-app-state.html).
