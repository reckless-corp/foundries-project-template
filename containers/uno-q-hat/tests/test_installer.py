"""Exercise failure paths without accessing GPIO or touching a real MCU."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

INSTALLER = Path(__file__).resolve().parents[1] / "install.sh"


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.state = self.root / "state"
        self.state.mkdir()
        board = self.root / "board"
        board.mkdir()
        (board / "model").write_bytes(b"Arduino UnoQ\0")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.script("uno-q-gpio-check", '#!/bin/sh\n[ "${GPIO_FAILURE:-0}" = 0 ] || exit 1\n[ "$#" -eq 0 ] || exec "$@"\n')
        self.script("openocd", '''#!/bin/sh
echo "$MCU_OPERATION" >> "$CALLS"
[ "${FAIL_OPERATION:-}" != "$MCU_OPERATION" ] || exit 1
if [ "$MCU_OPERATION" = backup ]; then
    dd if=/dev/zero of="$MCU_BACKUP" bs=1048576 count=2 2>/dev/null
fi
''')
        self.script("sleep", '#!/bin/sh\necho idle >> "$CALLS"\n')
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        MCU_STATE_DIR=str(self.state), MCU_BOARD_DIR=str(board),
                        MCU_READY_FILE=str(self.root / "ready"),
                        CALLS=str(self.root / "calls"))

    def script(self, name, text):
        path = self.bin / name
        path.write_text(text)
        path.chmod(0o755)

    def run_installer(self, operation="install", **env):
        return subprocess.run(["sh", str(INSTALLER), operation],
                              env=dict(self.env, **env), capture_output=True, text=True)

    def calls(self):
        path = self.root / "calls"
        return path.read_text().splitlines() if path.exists() else []

    def test_first_install_backs_up_before_programming(self):
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), ["backup", "install"])
        data = (self.state / "original.bin").read_bytes()
        self.assertEqual(len(data), 2097152)
        self.assertIn(hashlib.sha256(data).hexdigest(),
                      (self.state / "original.sha256").read_text())

    def test_repeat_install_preserves_original_backup(self):
        self.assertEqual(self.run_installer().returncode, 0)
        before = (self.state / "original.bin").stat().st_mtime_ns
        self.assertEqual(self.run_installer().returncode, 0)
        self.assertEqual(self.calls(), ["backup", "install", "install"])
        self.assertEqual((self.state / "original.bin").stat().st_mtime_ns, before)

    def test_backup_failure_prevents_flash_and_can_retry(self):
        self.assertNotEqual(self.run_installer(FAIL_OPERATION="backup").returncode, 0)
        self.assertEqual(self.calls(), ["backup"])
        self.assertFalse((self.state / "original.sha256").exists())
        self.assertEqual(self.run_installer().returncode, 0)

    def test_programming_failure_is_reported(self):
        self.assertNotEqual(self.run_installer(FAIL_OPERATION="install").returncode, 0)
        self.assertTrue((self.state / "original.sha256").exists())

    def test_corrupted_backup_blocks_install_and_restore(self):
        self.assertEqual(self.run_installer("backup").returncode, 0)
        (self.state / "original.bin").write_bytes(b"damaged")
        for operation in ["install", "restore"]:
            self.assertNotEqual(self.run_installer(operation).returncode, 0)
        self.assertEqual(self.calls(), ["backup"])

    def test_restore_requires_backup(self):
        self.assertNotEqual(self.run_installer("restore").returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_wrong_board_or_busy_gpio_prevents_openocd(self):
        self.assertNotEqual(self.run_installer(GPIO_FAILURE="1").returncode, 0)
        (self.root / "board/model").write_text("Different board")
        self.assertNotEqual(self.run_installer().returncode, 0)
        self.assertEqual(self.calls(), [])

    def test_verify_does_not_create_backup(self):
        self.assertNotEqual(self.run_installer("verify", FAIL_OPERATION="verify").returncode, 0)
        self.assertEqual(self.calls(), ["verify"])
        self.assertFalse((self.state / "original.sha256").exists())

    def test_service_becomes_ready_only_after_installation(self):
        result = self.run_installer("serve")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), ["backup", "install", "idle"])
        self.assertTrue((self.root / "ready").exists())

    def test_service_failure_clears_readiness_and_does_not_retry(self):
        (self.root / "ready").touch()
        result = self.run_installer("serve", FAIL_OPERATION="install")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), ["backup", "install", "idle"])
        self.assertFalse((self.root / "ready").exists())
        self.assertIn("remaining unhealthy", result.stderr)


if __name__ == "__main__":
    unittest.main()
