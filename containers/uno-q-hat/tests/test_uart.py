"""Run the real C parser and Linux controller against a pseudo-terminal MCU."""
import errno
import os
from pathlib import Path
import pty
import select
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UARTTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.build.cleanup)
        cls.controller = str(Path(cls.build.name) / 'hatctl')
        cls.parser_test = str(Path(cls.build.name) / 'protocol-test')
        cls.animation_test = str(Path(cls.build.name) / 'animation-test')
        cls.read_line_test = str(Path(cls.build.name) / 'read-line-test')
        for source, output in [(ROOT / 'controller/hatctl.c', cls.controller),
                               (ROOT / 'tests/read_line_test.c', cls.read_line_test),
                               (ROOT / 'tests/protocol_test.c', cls.parser_test),
                               (ROOT / 'tests/animation_test.c', cls.animation_test)]:
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'firmware/src'), str(source),
                            str(ROOT / 'firmware/src/protocol.c'),
                            str(ROOT / 'firmware/src/animation.c'), '-o', output], check=True)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = str(Path(self.tmp.name) / 'control.sock')
        self.master, self.slave = pty.openpty()
        self.addCleanup(os.close, self.slave)
        self.addCleanup(os.close, self.master)
        self.env = dict(os.environ, MCU_UART=os.ttyname(self.slave), MCU_SOCKET=self.path)
        self.boot = 1
        self.proto = 1
        self.anim = ' anim=64'
        self.stopped_frame = '0200' * 8
        self.silent = False
        self.noise = False
        self.reject = None
        self.commands = []
        self.stopped = threading.Event()
        self.worker = threading.Thread(target=self.mcu, daemon=True)
        self.worker.start()
        self.addCleanup(self.stop_worker)
        self.log = tempfile.TemporaryFile()
        self.addCleanup(self.log.close)
        self.daemon = subprocess.Popen([self.controller, 'daemon'], env=self.env,
                                       stdout=self.log, stderr=self.log)
        self.addCleanup(self.stop_daemon)
        self.wait_for(lambda: os.path.exists(self.path))

    def stop_worker(self):
        self.stopped.set()
        self.worker.join(timeout=2)
        self.assertFalse(self.worker.is_alive())

    def stop_daemon(self):
        if self.daemon.poll() is None:
            self.daemon.terminate()
            self.daemon.wait(timeout=3)

    def wait_for(self, predicate, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            if self.daemon.poll() is not None:
                self.log.seek(0)
                self.fail(self.log.read().decode())
            time.sleep(.02)
        self.fail('Condition did not become true')

    def mcu(self):
        pending = b''
        while not self.stopped.is_set():
            if not select.select([self.master], [], [], .05)[0]:
                continue
            try:
                pending += os.read(self.master, 4096)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    continue
                raise
            while b'\n' in pending:
                line, pending = pending.split(b'\n', 1)
                if b' ' not in line:
                    continue
                ident, raw = line.split(b' ', 1)
                command = raw.decode('ascii')
                self.commands.append(command)
                if self.silent:
                    continue
                if command == self.reject:
                    result = 'ERR IO'
                elif command == 'HELLO':
                    result = f'OK proto={self.proto} firmware=test width=13 height=8{self.anim} boot={self.boot:08x}'
                elif command == 'ANIM STOP':
                    result = 'OK frame=' + self.stopped_frame
                elif command == 'PING':
                    result = f'OK boot={self.boot:08x} uptime_ms=100'
                else:
                    result = 'OK'
                if self.noise:
                    os.write(self.master, b'garbage\n999999 OK stale\n' + b'x' * 200 + b'\n')
                wire = ident + b' ' + result.encode() + b'\n'
                # Deliberately fragment every MCU response.
                os.write(self.master, wire[:3])
                os.write(self.master, wire[3:])

    def cli(self, *args):
        return subprocess.run([self.controller, *args], env=self.env,
                              capture_output=True, text=True, timeout=8)

    def test_parser_validation_and_resynchronization(self):
        subprocess.run([self.parser_test], check=True)

    def test_read_line_boundaries_and_resynchronization(self):
        subprocess.run([self.read_line_test], check=True)

    def test_live_commands_and_stale_fragmented_responses(self):
        self.noise = True
        result = self.cli('HELLO')
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn('proto=1', result.stdout)
        self.assertEqual(self.cli('DISPLAY', 'OFF').stdout, 'OK\n')
        self.assertEqual(self.cli('FRAME', 'ff1f' * 8).stdout, 'OK\n')
        self.assertEqual(self.cli('PING').returncode, 0)

    def test_reset_restores_last_acknowledged_state(self):
        self.assertEqual(self.cli('FRAME', '0100' * 8).returncode, 0)
        self.assertEqual(self.cli('DISPLAY', 'OFF').returncode, 0)
        start = len(self.commands)
        self.boot += 1
        self.assertEqual(self.cli('PING').returncode, 0)
        commands = self.commands[start:]
        self.assertIn('HELLO', commands)
        hello = commands.index('HELLO')
        self.assertEqual(commands[hello + 1:hello + 3], ['FRAME ' + '0100' * 8, 'DISPLAY OFF'])

    def test_timeout_fails_health_then_recovers(self):
        self.assertEqual(self.cli('PING').returncode, 0)
        self.silent = True
        result = self.cli('PING')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('UNAVAILABLE', result.stdout)
        self.silent = False
        self.assertEqual(self.cli('PING').returncode, 0)

    def test_protocol_mismatch_blocks_commands(self):
        self.assertEqual(self.cli('PING').returncode, 0)
        self.proto = 2
        self.boot += 1
        self.assertNotEqual(self.cli('DISPLAY', 'OFF').returncode, 0)
        self.assertNotIn('DISPLAY OFF', self.commands)
        self.proto = 1
        self.assertEqual(self.cli('PING').returncode, 0)

    def test_rejected_command_does_not_replace_desired_state(self):
        self.assertEqual(self.cli('DISPLAY', 'OFF').returncode, 0)
        self.reject = 'DISPLAY ON'
        self.assertEqual(self.cli('DISPLAY', 'ON').stdout, 'ERR IO\n')
        self.reject = None
        self.boot += 1
        start = len(self.commands)
        self.assertEqual(self.cli('PING').returncode, 0)
        self.assertIn('DISPLAY OFF', self.commands[start:])
        self.assertNotIn('DISPLAY ON', self.commands[start:])

    def test_invalid_client_input_never_reaches_uart(self):
        self.assertEqual(self.cli('PING').returncode, 0)
        for command, expected in [(b'FRAME ' + b'ffff' * 8, b'ERR BAD_ARGUMENT'),
                                  (b'x' * 4097, b'ERR BAD_LINE'),
                                  (b'PING\x00extra', b'ERR BAD_LINE')]:
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(6)
                client.connect(self.path)
                client.sendall(command + b'\n')
                response = b''
                while not response.endswith(b'\n'):
                    response += client.recv(256)
                self.assertEqual(response.strip(), expected)
        self.assertFalse(any(c.startswith('FRAME') for c in self.commands))

    def test_animation_timing_and_atomic_replacement(self):
        subprocess.run([self.animation_test], check=True)

    def test_animation_upload_and_reset_recovery(self):
        args = ['ANIM', '3', '500:' + '0100' * 8, '250:' + '0000' * 8]
        expected = ['ANIM BEGIN 2', 'ANIM ADD 500 ' + '0100' * 8,
                    'ANIM ADD 250 ' + '0000' * 8, 'ANIM PLAY 3']
        self.assertEqual(self.cli(*args).stdout, 'OK\n')
        self.assertEqual(self.commands[-4:], expected)
        self.assertEqual(self.cli('DISPLAY', 'OFF').returncode, 0)
        self.boot += 1
        start = len(self.commands)
        self.assertEqual(self.cli('PING').returncode, 0)
        commands = self.commands[start:]
        hello = commands.index('HELLO')
        self.assertEqual(commands[hello + 1:hello + 6], expected + ['DISPLAY OFF'])

    def test_maximum_animation_fits_local_command_and_short_uart_lines(self):
        frames = ['60000:' + 'ff1f' * 8] * 64
        self.assertEqual(self.cli('ANIM', '4294967295', *frames).stdout, 'OK\n')
        self.assertIn('ANIM BEGIN 64', self.commands)
        self.assertEqual(self.commands.count('ANIM ADD 60000 ' + 'ff1f' * 8), 64)
        self.assertEqual(self.commands[-1], 'ANIM PLAY 4294967295')
        self.assertTrue(all(len(c) + 11 <= 128 for c in self.commands))
        self.assertEqual(self.cli('ANIM', '0', *frames, frames[0]).returncode, 2)

    def test_invalid_animations_do_not_upload(self):
        valid = '100:' + '0100' * 8
        for args in [('0',), ('-1', valid), ('4294967296', valid),
                     ('0', '19:' + '0000' * 8), ('0', '60001:' + '0000' * 8),
                     ('0', valid, '100:bad'), ('0', '100:' + 'ffff' * 8),
                     ('0', valid + 'extra'), ('BEGIN', '2'), ('PLAY', '1')]:
            self.assertEqual(self.cli('ANIM', *args).returncode, 2, args)
        self.assertFalse(any(c.startswith('ANIM') for c in self.commands))
        # Bypass CLI validation to exercise the daemon's independent validation.
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(6)
            client.connect(self.path)
            client.sendall(('ANIM 0 ' + valid + ' 100:bad\n').encode())
            reply = b''
            while not reply.endswith(b'\n'):
                reply += client.recv(256)
            self.assertEqual(reply, b'ERR BAD_ARGUMENT\n')
        self.assertFalse(any(c.startswith('ANIM') for c in self.commands))

    def test_rejected_upload_preserves_previous_animation(self):
        self.assertEqual(self.cli('ANIM', '0', '100:' + '0100' * 8).returncode, 0)
        self.reject = 'ANIM ADD 200 ' + '0200' * 8
        self.assertEqual(self.cli('ANIM', '2', '200:' + '0200' * 8).stdout, 'ERR IO\n')
        self.reject = None
        self.boot += 1
        start = len(self.commands)
        self.assertEqual(self.cli('PING').returncode, 0)
        self.assertIn('ANIM ADD 100 ' + '0100' * 8, self.commands[start:])
        self.assertNotIn('ANIM PLAY 2', self.commands)

    def test_animation_stop_restores_actual_stopped_frame(self):
        self.assertEqual(self.cli('ANIM', '0', '100:' + '0100' * 8).returncode, 0)
        self.assertEqual(self.cli('ANIM', 'STOP').stdout, 'OK\n')
        self.boot += 1
        start = len(self.commands)
        self.assertEqual(self.cli('PING').returncode, 0)
        self.assertIn('FRAME ' + self.stopped_frame, self.commands[start:])
        self.assertFalse(any(c.startswith('ANIM') for c in self.commands[start:]))

    def test_static_command_replaces_animation_for_recovery(self):
        for args in [('FRAME', '0400' * 8), ('SHOW', 'HAT')]:
            self.assertEqual(self.cli('ANIM', '0', '100:' + '0100' * 8).returncode, 0)
            self.assertEqual(self.cli(*args).returncode, 0)
            self.boot += 1
            start = len(self.commands)
            self.assertEqual(self.cli('PING').returncode, 0)
            self.assertIn(' '.join(args), self.commands[start:])
            self.assertFalse(any(c.startswith('ANIM') for c in self.commands[start:]))

    def test_old_firmware_reports_unsupported_before_upload(self):
        self.assertEqual(self.cli('PING').returncode, 0)
        self.anim = ''
        self.boot += 1
        self.assertEqual(self.cli('ANIM', '0', '100:' + '0100' * 8).stdout,
                         'ERR UNSUPPORTED\n')
        self.assertFalse(any(c.startswith('ANIM') for c in self.commands))

    def test_second_daemon_cannot_remove_live_socket(self):
        result = self.cli('daemon')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.cli('PING').returncode, 0)

    def test_shutdown_releases_uart_and_socket(self):
        self.assertEqual(self.cli('PING').returncode, 0)
        self.stop_daemon()
        self.assertFalse(os.path.exists(self.path))
        self.daemon = subprocess.Popen([self.controller, 'daemon'], env=self.env,
                                       stdout=self.log, stderr=self.log)
        self.wait_for(lambda: os.path.exists(self.path))
        self.assertEqual(self.cli('PING').returncode, 0)
