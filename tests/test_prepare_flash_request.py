import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_flash_request import load_session_baseline, sha


class SessionBaselineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.session = Path(self.temp.name)
        self.image = b"\x5a" * 1_048_576
        self.digest = sha(self.image)

    def write_fixture(self, filename="baseline.bin", **updates):
        state = dict(baseline_file=filename, baseline_sha256=self.digest,
                     blocked=False, reset_pending=False, needs_observation=False)
        state.update(updates)
        (self.session / "state.json").write_text(json.dumps(state), encoding="utf-8")
        image_file = self.session / filename
        image_file.parent.mkdir(parents=True, exist_ok=True)
        image_file.write_bytes(self.image)
        receipt_name = "baseline-deployment.json" if image_file.name == "baseline.bin" else "deployment.json"
        receipt_file = image_file.parent / receipt_name
        receipt_file.write_text(json.dumps(dict(status="written_and_readback_verified",
                                                full_readback_sha256=self.digest)), encoding="utf-8")
        return image_file, receipt_file

    def test_verified_baseline_is_not_pinned_to_v32(self):
        self.write_fixture()
        image, digest = load_session_baseline(self.session)
        self.assertEqual(image, self.image)
        self.assertEqual(digest, self.digest)

    def test_current_deployment_receipt_is_adjacent_to_readback(self):
        self.write_fixture("runs/verified/final-1.bin")
        self.assertEqual(load_session_baseline(self.session), (self.image, self.digest))

    def test_blocked_or_pending_session_is_rejected(self):
        for flag in ("blocked", "reset_pending", "needs_observation"):
            with self.subTest(flag=flag):
                self.write_fixture(**{flag: True})
                with self.assertRaisesRegex(ValueError, "idle verified baseline"):
                    load_session_baseline(self.session)

    def test_readback_must_match_protected_identity(self):
        image_file, _ = self.write_fixture()
        image_file.write_bytes(b"\x00" + self.image[1:])
        with self.assertRaisesRegex(ValueError, "readback bytes differ"):
            load_session_baseline(self.session)

    def test_readback_must_be_complete_even_with_matching_digest(self):
        self.image = self.image[:-1]
        self.digest = sha(self.image)
        self.write_fixture()
        with self.assertRaisesRegex(ValueError, "readback bytes differ"):
            load_session_baseline(self.session)

    def test_receipt_must_verify_the_same_full_image(self):
        for receipt in (dict(status="written_without_verification", full_readback_sha256=self.digest),
                        dict(status="written_and_readback_verified", full_readback_sha256="0" * 64)):
            with self.subTest(receipt=receipt):
                _, receipt_file = self.write_fixture()
                receipt_file.write_text(json.dumps(receipt), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "matching full-readback receipt"):
                    load_session_baseline(self.session)

    def test_twice_verified_receipt_remains_supported(self):
        _, receipt_file = self.write_fixture()
        receipt_file.write_text(json.dumps(dict(status="written_and_twice_readback_verified",
                                                full_readback_sha256=self.digest)), encoding="utf-8")
        self.assertEqual(load_session_baseline(self.session)[1], self.digest)

    def test_baseline_cannot_escape_protected_session(self):
        self.write_fixture()
        state = json.loads((self.session / "state.json").read_text(encoding="utf-8"))
        state["baseline_file"] = "../outside.bin"
        (self.session / "state.json").write_text(json.dumps(state), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "escaped protected session"):
            load_session_baseline(self.session)

    def test_invalid_digest_is_rejected(self):
        for digest in (None, "", "x" * 64):
            with self.subTest(digest=digest):
                self.write_fixture(baseline_sha256=digest)
                with self.assertRaisesRegex(ValueError, "SHA-256 is invalid"):
                    load_session_baseline(self.session)


if __name__ == "__main__":
    unittest.main()
