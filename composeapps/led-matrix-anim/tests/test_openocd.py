"""Execute the actual Tcl installer against simulated OpenOCD commands."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "openocd/operate.tcl"


@unittest.skipUnless(shutil.which("tclsh"), "tclsh is required for OpenOCD control-flow tests")
class OpenOCDTests(unittest.TestCase):
    def run_script(self, operation="install", **env):
        harness = '''
proc init {} {if {[info exists ::env(CONNECT_FAIL)]} {error "connection failed"}}
proc reset {args} {puts "RESET $args"}
proc read_memory {args} {return {170 0 0 0 0 0 0 0 0 0 0 0}}
proc sleep {args} {}
proc halt {} {}
proc resume {} {}
proc get_reg {args} {
    if {[info exists ::env(BOOT_ROM)]} {return {pc 0x0bf94cd6}}
    return {pc 0x08003a34}
}
proc flash {args} {
    if {[lindex $args 0] eq "write_image"} {
        puts "WRITE"
        if {[info exists ::env(WRITE_FAIL)]} {error "write failed"}
    }
}
set verifies 0
proc verify_image {args} {
    incr ::verifies
    puts "VERIFY"
    if {[info exists ::env(MISMATCH)] && $::verifies == 1} {error "mismatch"}
    if {[info exists ::env(VERIFY_FAIL)] && $::verifies > 1} {error "bad write"}
}
proc echo {args} {puts $args}
proc shutdown {} {puts "SUCCESS"}
if {[catch {source $::env(SCRIPT)} err]} {puts stderr $err; exit 1}
'''
        return subprocess.run(["tclsh"], input=harness, capture_output=True, text=True,
                              env=dict(os.environ, SCRIPT=str(SCRIPT),
                                       MCU_OPERATION=operation, MCU_IMAGE="/firmware/image.elf", **env))

    def test_matching_firmware_skips_write(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("WRITE", result.stdout)
        self.assertIn("RESET run", result.stdout)

    def test_changed_firmware_is_written_and_verified(self):
        result = self.run_script(MISMATCH="1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count("VERIFY"), 2)
        self.assertEqual(result.stdout.count("WRITE"), 1)

    def test_connection_failure_never_writes(self):
        result = self.run_script(CONNECT_FAIL="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("WRITE", result.stdout)

    def test_failed_write_or_verification_never_reports_success(self):
        for failure in ["WRITE_FAIL", "VERIFY_FAIL"]:
            result = self.run_script(MISMATCH="1", **{failure: "1"})
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("SUCCESS", result.stdout)

    def test_verify_mode_never_writes(self):
        result = self.run_script("verify", MISMATCH="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("WRITE", result.stdout)

    def test_boot_rom_execution_is_not_success(self):
        result = self.run_script(BOOT_ROM="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SUCCESS", result.stdout)
