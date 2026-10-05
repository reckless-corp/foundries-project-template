# SPDX-License-Identifier: Apache-2.0
init
reset halt
flash info 0
set options [read_memory 0x40022040 32 12]
echo "FLASH option registers at 0x40022040: $options"
# The standalone image is built for TZEN=0 and unprotected flash (RDP=0xAA).
if {([lindex $options 0] & 0x800000ff) != 0xaa} {
    error "Unsupported TrustZone/read-protection options; refusing to change them"
}

set operation $::env(MCU_OPERATION)
if {$operation eq "backup"} {
    # Read-only snapshot of all 2 MiB plus option registers. Never write options.
    dump_image $::env(MCU_BACKUP) 0x08000000 0x200000
    verify_image $::env(MCU_BACKUP) 0x08000000 bin
} elseif {$operation eq "install" || $operation eq "verify"} {
    if {[catch {verify_image $::env(MCU_IMAGE)}]} {
        if {$operation eq "verify"} {error "Firmware does not match"}
        echo "Firmware differs; programming"
        flash write_image erase $::env(MCU_IMAGE)
        verify_image $::env(MCU_IMAGE)
    } else {
        echo "Firmware already matches; no flash write"
    }
} elseif {$operation eq "restore"} {
    flash write_image erase $::env(MCU_BACKUP) 0x08000000 bin
    verify_image $::env(MCU_BACKUP) 0x08000000 bin
} else {
    error "Unknown operation"
}
reset run
if {$operation eq "install" || $operation eq "verify"} {
    # A verified flash image is insufficient if BOOT0 selects the ST ROM.
    sleep 200
    halt
    set pc [dict get [get_reg {pc}] pc]
    resume
    if {$pc < 0x08000000 || $pc >= 0x08200000} {
        error "MCU did not enter application flash: PC=$pc"
    }
    echo "MCU executing application flash: PC=$pc"
}
shutdown
