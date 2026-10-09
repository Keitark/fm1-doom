import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pi32_stack import frame_instructions, usb_diagnostic_budget


class EmittedStackTests(unittest.TestCase):
    def test_register_saves_and_local_allocations(self):
        code = [(0x100, "[--sp] = {rets, r15-r4}"),
                (0x102, "sp += -84"), (0x104, "sp += 84"),
                (0x106, "{rets, r15-r4} = [sp++]")]
        self.assertEqual(frame_instructions(code, 0x100, 8)["frame_bytes"], 136)

    def test_variable_instruction_length_and_annotation(self):
        code = [(0x100, "call 100 <target : 166 >"),
                (0x102, "call 100 <target : 16a >"),
                (0x106, "call 100 <target : 170 >"), (0x10c, "rets")]
        self.assertEqual(frame_instructions(code, 0x100, 14)["direct_call_addresses"],
                         [0x166, 0x16a, 0x170])
        code[0] = (0x100, "call 100 <target : 168 >")
        with self.assertRaisesRegex(ValueError, "annotation"):
            frame_instructions(code, 0x100, 14)

    def test_unknown_dynamic_stack_write_is_rejected(self):
        for instruction in ("sp = r0", "sp += r2", "[--sp] = {r16}"):
            with self.assertRaises(ValueError):
                frame_instructions([(0x100, instruction)], 0x100, 2)

    @staticmethod
    def report():
        sizes = {"fm1_doom_usb_task": 136, "command.2434": 16,
                 "fm1_doom_usb_protocol_status": 956, "fm1_doom_usb_protocol_trace": 932,
                 "fm1_doom_usb_protocol_audio": 1100, "fm1_doom_usb_protocol_volume": 640, "game_status": 720,
                 "frame_info.2447": 204, "frame_read.2449": 424, "edit_status.2450": 600,
                 "fm1_doom_usb_protocol_usb_audio": 480, "fm1_usb_audio_target_status": 128,
                 "snprintf": 12, "vsnprintf": 136, "decimal": 24, "repeat": 16, "string": 20}
        frames = {name: {"frame_bytes": size, "direct_calls": [], "indirect_calls": []}
                  for name, size in sizes.items()}
        frames["fm1_doom_usb_task"]["direct_calls"] = ["command.2434", "fm1_doom_usb_protocol_status"]
        frames["command.2434"]["direct_calls"] = list(sizes)[2:11]
        frames["fm1_doom_usb_protocol_usb_audio"]["direct_calls"] = ["fm1_usb_audio_target_status"]
        frames["snprintf"]["direct_calls"] = ["vsnprintf"]
        frames["vsnprintf"]["direct_calls"] = ["decimal", "repeat", "string"]
        return {"functions": frames}

    def test_fixed_diagnostic_chain_retains_sdk_margin(self):
        budget = usb_diagnostic_budget(self.report(), 4096)
        self.assertEqual(budget["diagnostic_chain_bytes"], 1424)
        self.assertEqual(budget["remaining_after_chain_and_margin_bytes"], 1648)

    def test_aggregated_lto_frame_cannot_ship(self):
        report = self.report()
        report["functions"]["fm1_doom_usb_task"]["frame_bytes"] = 3192
        with self.assertRaisesRegex(ValueError, "exceeds"):
            usb_diagnostic_budget(report, 4096)

    def test_usb_audio_nested_formatter_counts_toward_peak(self):
        report = self.report()
        report["functions"]["fm1_usb_audio_target_status"]["frame_bytes"] = 800
        self.assertEqual(usb_diagnostic_budget(report, 4096)["diagnostic_chain_bytes"], 1604)
        del report["functions"]["fm1_usb_audio_target_status"]
        with self.assertRaisesRegex(ValueError, "audio formatter"):
            usb_diagnostic_budget(report, 4096)
        del report["functions"]["fm1_doom_usb_protocol_audio"]
        with self.assertRaisesRegex(ValueError, "boundary"):
            usb_diagnostic_budget(report, 4096)

    def test_volume_reply_is_covered_by_the_stack_gate(self):
        report = self.report()
        del report["functions"]["fm1_doom_usb_protocol_volume"]
        with self.assertRaisesRegex(ValueError, "boundary"):
            usb_diagnostic_budget(report, 3072)
        report = self.report()
        report["functions"]["fm1_doom_usb_protocol_volume"]["frame_bytes"] = 1800
        with self.assertRaisesRegex(ValueError, "exceeds"):
            usb_diagnostic_budget(report, 3072)

    def test_edit_reply_is_covered_by_the_stack_gate(self):
        report = self.report()
        del report["functions"]["edit_status.2450"]
        with self.assertRaisesRegex(ValueError, "boundary"):
            usb_diagnostic_budget(report, 3072)
        report = self.report()
        report["functions"]["edit_status.2450"]["frame_bytes"] = 1800
        with self.assertRaisesRegex(ValueError, "exceeds"):
            usb_diagnostic_budget(report, 3072)

    def test_unreviewed_callback_and_nested_formatting_fail_closed(self):
        for name, field, value in (("game_status", "indirect_calls", ["call r0"]),
                                   ("game_status", "tail_calls", ["goto r0"]),
                                   ("game_status", "direct_calls", ["fm1_doom_usb_protocol_audio"]),
                                   ("snprintf", "direct_calls", ["vsnprintf", "unknown"]),
                                   ("vsnprintf", "direct_calls", ["unknown"]),
                                   ("decimal", "direct_calls", ["recursive"])):
            report = copy.deepcopy(self.report())
            report["functions"][name][field] = value
            with self.assertRaises(ValueError):
                usb_diagnostic_budget(report, 4096)

    def test_external_tail_call_is_reported(self):
        local = [(0x100, "goto 0"), (0x102, "rets")]
        self.assertFalse(frame_instructions(local, 0x100, 4)["tail_calls"])
        local[0] = (0x100, "goto 100")
        self.assertEqual(len(frame_instructions(local, 0x100, 4)["tail_calls"]), 1)


if __name__ == "__main__":
    unittest.main()
