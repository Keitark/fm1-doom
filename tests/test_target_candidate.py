import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_target_candidate import task_heap_budget, usb_heap_allocations


class TargetBudgetTests(unittest.TestCase):
    def test_reviewed_task_budget(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["minimum_task_heap_bytes"], 42_584)
        self.assertEqual(budget["required_linker_heap_bytes"], 48_716)

    def test_old_stack_exceeds_previous_linker_heap(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        source = source.replace('"fm1_doom", 10, 2048', '"fm1_doom", 10, 8192')
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["required_linker_heap_bytes"], 73_292)
        self.assertGreater(budget["required_linker_heap_bytes"], 48_332)

    def test_usb_allocation_measurement(self):
        cdc = 'define void @cdc_register(i8 %id) {\n call i8* @zalloc(i32 200)\n call i8* @malloc(i32 64)\n}\n'
        config = 'define i32 @usb_config(i8 %id) {\n call i8* @zalloc(i32 972)\n}\n'
        self.assertEqual(usb_heap_allocations(cdc, config)["total_requested_bytes"], 1236)
        with self.assertRaises(ValueError):
            usb_heap_allocations(cdc, config.replace("972", "1024"))


if __name__ == "__main__":
    unittest.main()
