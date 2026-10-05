import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from capture_usb_frame import capture, display_pixels


class FakeSerial:
    def __init__(self, bad_id=False):
        self.commands = []
        self.pending = bytearray()
        self.data = bytes([0]) * 16000 + bytes([0, 248]) + bytes(510)
        self.bad_id = bad_id

    def write(self, command):
        self.commands.append(command)
        if command == b"DOOM GAME\n":
            reply = "DOOM GAME stage=4 x=69206016 y=-236978176\n"
        elif command == b"DOOM FRAME BEGIN\n":
            reply = "OK DOOM FRAME REQUESTED\n"
        elif command == b"DOOM FRAMEINFO\n":
            reply = "DOOM FRAMEINFO state=2 ready=1 id=7 width=160 height=100 bytes=16512 coarse=1 menu=0\n"
        elif command.startswith(b"DOOM FRAME READ "):
            offset = int(command.split()[-1])
            data = self.data[offset:offset + 96]
            reply = f"DOOM FRAME id={8 if self.bad_id else 7} offset={offset} bytes={len(data)} data={data.hex()}\n"
        else:
            reply = "OK DOOM FRAME END\n"
        self.pending.extend(reply.encode())

    def readline(self):
        # Simulate replies split across serial read timeouts.
        count = min(13, len(self.pending))
        newline = self.pending.find(b"\n", 0, count)
        if newline >= 0:
            count = newline + 1
        result = bytes(self.pending[:count])
        del self.pending[:count]
        return result


class CaptureTests(unittest.TestCase):
    def test_fragmented_frame_and_565_display(self):
        port = FakeSerial()
        data, metadata = capture(port)
        self.assertEqual(data, port.data)
        self.assertEqual(metadata["frame"]["id"], 7)
        self.assertEqual(port.commands[-1], b"DOOM FRAME END\n")
        source, display = display_pixels(data, 1, 0)
        self.assertEqual(source, bytes([255, 0, 0]) * 16000)
        self.assertEqual(display, bytes([255, 0, 0]) * 57600)

    def test_frame_identity_change_always_releases_hold(self):
        port = FakeSerial(bad_id=True)
        with self.assertRaisesRegex(ValueError, "sequence"):
            capture(port)
        self.assertEqual(port.commands[-1], b"DOOM FRAME END\n")


if __name__ == "__main__":
    unittest.main()
