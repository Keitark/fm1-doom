import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from doom_usb_overlay import capture_link


class CaptureLinkTests(unittest.TestCase):
    def fixture(self):
        import re
        source = (ROOT / "src/fm1_usb_audio_profile.c").read_text()
        body = re.search(r"descriptor\[[^]]+\]\s*=\s*\{([^}]+)", source)[1]
        descriptor = bytes(int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body))
        image = descriptor + bytes((18, 1, 0, 2, 0xef, 2, 1)) + bytes(11)
        nm = "02000120 00000063 t fm1_usb_audio_capture_descriptor\n02000183 00000012 t fm1_usb_device_descriptor\n01c60000 00000100 b audio_dma\n"
        return nm, image, source

    def test_capture_link(self):
        nm, image, source = self.fixture()
        result = capture_link(nm, image, 0x02000120, source)
        self.assertEqual(result["interfaces"], [2, 3])
        self.assertEqual(result["static_endpoint_dma_bytes"], 256)

    def test_corrupted_descriptors_and_dma_fail(self):
        nm, image, source = self.fixture()
        for index in (0, 80, 103):
            broken = bytearray(image)
            broken[index] ^= 1
            with self.assertRaises(ValueError):
                capture_link(nm, broken, 0x02000120, source)
        for broken in (nm.replace("01c60000", "01c60004"), nm.replace("00000100", "00000080"),
                       nm.replace("01c60000", "02060000"), nm + "02070000 00000100 t usb_g_ep_write\n"):
            with self.assertRaises(ValueError):
                capture_link(broken, image, 0x02000120, source)


if __name__ == "__main__":
    unittest.main()
