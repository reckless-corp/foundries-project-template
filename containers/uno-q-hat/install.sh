#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
operation=${1:-install}
case "$operation" in
    serve)
        # A completed one-shot does not restart when Docker starts after boot.
        # Never loop over a failing flash attempt. UART recovery does not reflash.
        if sh "$0" install; then
            mkdir -p "$(dirname "${MCU_SOCKET:-/run/uno-q-hat/control.sock}")"
            exec uno-q-hatctl daemon
        else
            echo 'MCU installation failed; remaining unhealthy until manually restarted' >&2
        fi
        exec sleep infinity
        ;;
    install|backup|verify|restore) ;;
    *) echo "Usage: $0 {serve|install|backup|verify|restore}" >&2; exit 2 ;;
esac
state=${MCU_STATE_DIR:-/state}
board=${MCU_BOARD_DIR:-/board}
config=${MCU_CONFIG_DIR:-/app/openocd}
export MCU_IMAGE=${MCU_IMAGE:-/firmware/zephyr.elf}
model=$(tr -d '\000' < "$board/model")
[ "$model" = 'Arduino UnoQ' ] || { echo "Not an Arduino UnoQ: $model" >&2; exit 1; }
uno-q-gpio-check
mkdir -p "$state"
# Shared backup volume also serializes manual operations with compose startup.
exec 9>"$state/lock"
flock -n 9 || { echo 'Another MCU operation is in progress' >&2; exit 1; }
export MCU_BACKUP="$state/original.bin"
run_openocd() {
    export MCU_OPERATION=$1
    uno-q-gpio-check openocd -f "$config/uno-q.cfg" -f "$config/operate.tcl"
}
valid_backup() {
    [ -f "$state/original.sha256" ] && (cd "$state" && sha256sum -c original.sha256)
}
backup() {
    if [ -e "$state/original.sha256" ]; then
        valid_backup
        echo 'Original backup already exists; preserving it'
        return
    fi
    export MCU_BACKUP="$state/original.bin.partial"
    if ! run_openocd backup > "$state/backup.log" 2>&1; then
        cat "$state/backup.log" >&2
        exit 1
    fi
    cat "$state/backup.log"
    [ "$(wc -c < "$MCU_BACKUP")" -eq 2097152 ]
    mv "$MCU_BACKUP" "$state/original.bin"
    export MCU_BACKUP="$state/original.bin"
    (cd "$state" && sha256sum original.bin > original.sha256.partial && mv original.sha256.partial original.sha256)
    sync
}
case "$operation" in
    backup) backup ;;
    install) backup; run_openocd install ;;
    verify) run_openocd verify ;;
    restore) valid_backup || { echo 'Missing or damaged original backup' >&2; exit 1; }; run_openocd restore ;;
esac
