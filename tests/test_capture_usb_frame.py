import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from capture_usb_frame import capture, display_pixels


class FakeClock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeSerial:
    def __init__(self, clock, bad_id=False, hello_drops=0, hello_delay=0,
                 stall_frame=False):
        self.clock = clock
        self.commands = []
        self.pending = bytearray()
        self.delayed = []
        self.data = bytes([0]) * 16000 + bytes([0, 248]) + bytes(510)
        self.bad_id = bad_id
        self.hello_drops = hello_drops
        self.hello_delay = hello_delay
        self.stall_frame = stall_frame

    def write(self, command):
        self.commands.append(command)
        if command == b"HELLO\n":
            if self.hello_drops:
                self.hello_drops -= 1
                return
            reply = "FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n"
            if self.hello_delay:
                self.delayed.append((self.clock.now + self.hello_delay, reply.encode()))
                return
        elif command == b"DOOM GAME\n":
            reply = "DOOM GAME stage=4 x=69206016 y=-236978176\n"
        elif command == b"DOOM FRAME BEGIN\n":
            reply = "OK DOOM FRAME REQUESTED\n"
        elif command == b"DOOM FRAMEINFO\n":
            reply = "DOOM FRAMEINFO state=2 ready=1 id=7 width=160 height=100 bytes=16512 coarse=1 menu=0\n"
        elif command.startswith(b"DOOM FRAME READ "):
            if self.stall_frame:
                return
            offset = int(command.split()[-1])
            data = self.data[offset:offset + 96]
            reply = f"DOOM FRAME id={8 if self.bad_id else 7} offset={offset} bytes={len(data)} data={data.hex()}\n"
        else:
            reply = "OK DOOM FRAME END\n"
        self.pending.extend(reply.encode())

    def readline(self):
        self.clock.sleep(0.001 if self.pending else 0.1)
        for ready, reply in self.delayed[:]:
            if ready <= self.clock.now:
                self.pending.extend(reply)
                self.delayed.remove((ready, reply))
        # Simulate replies split across serial read timeouts.
        count = min(13, len(self.pending))
        newline = self.pending.find(b"\n", 0, count)
        if newline >= 0:
            count = newline + 1
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.time_patch = patch("capture_usb_frame.time", self.clock)
        self.time_patch.start()
        self.addCleanup(self.time_patch.stop)

    def test_fragmented_frame_and_565_display(self):
        port = FakeSerial(self.clock)
        data, metadata = capture(port)
        self.assertEqual(data, port.data)
        self.assertEqual(metadata["frame"]["id"], 7)
        self.assertEqual(port.commands[-1], b"DOOM FRAME END\n")
        source, display = display_pixels(data, 1, 0)
        self.assertEqual(source, bytes([255, 0, 0]) * 16000)
        self.assertEqual(display, bytes([255, 0, 0]) * 57600)

    def test_frame_identity_change_always_releases_hold(self):
        port = FakeSerial(self.clock, bad_id=True)
        with self.assertRaisesRegex(ValueError, "sequence"):
            capture(port)
        self.assertEqual(port.commands[-1], b"DOOM FRAME END\n")

    def test_lost_initial_hello_is_retried_before_game(self):
        port = FakeSerial(self.clock, hello_drops=1)
        data, _ = capture(port)
        self.assertEqual(data, port.data)
        self.assertEqual(port.commands[:3], [b"HELLO\n", b"HELLO\n", b"DOOM GAME\n"])
        self.assertGreaterEqual(self.clock.now, 2)

    def test_delayed_hello_is_accepted_during_retry(self):
        port = FakeSerial(self.clock, hello_delay=1.2)
        data, _ = capture(port)
        self.assertEqual(data, port.data)
        self.assertEqual(port.commands[:3], [b"HELLO\n", b"HELLO\n", b"DOOM GAME\n"])

    def test_unresponsive_startup_is_bounded_without_frame_hold(self):
        port = FakeSerial(self.clock, hello_drops=3)
        with self.assertRaisesRegex(TimeoutError, "HELLO"):
            capture(port)
        self.assertEqual(port.commands, [b"HELLO\n"] * 3)
        self.assertLessEqual(self.clock.now, 4.3)

    def test_capture_deadline_includes_startup_and_releases_stalled_hold(self):
        port = FakeSerial(self.clock, hello_drops=1, stall_frame=True)
        with self.assertRaisesRegex(TimeoutError, "capture deadline"):
            capture(port)
        self.assertGreaterEqual(self.clock.now, 12)
        self.assertLessEqual(self.clock.now, 12.1)
        self.assertEqual(port.commands[-1], b"DOOM FRAME END\n")


if __name__ == "__main__":
    unittest.main()
