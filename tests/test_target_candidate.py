import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_target_candidate import iis_heap_allocations, task_heap_budget, usb_heap_allocations


class TargetBudgetTests(unittest.TestCase):
    def test_reviewed_task_budget(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["minimum_task_heap_bytes"], 38_488)
        self.assertEqual(budget["required_linker_heap_bytes"], 44_620)

    def test_old_stack_exceeds_previous_linker_heap(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        source = source.replace('"fm1_doom", 10, 2048', '"fm1_doom", 10, 8192')
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["required_linker_heap_bytes"], 69_196)
        self.assertGreater(budget["required_linker_heap_bytes"], 48_332)

    def test_audio_dma_is_reserved_before_runtime_margin(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        budget = task_heap_budget(source, 1236, 1024)
        self.assertEqual(budget["minimum_task_heap_bytes"], 38_488)
        self.assertEqual(budget["audio_dynamic_heap_bytes"], 1024)
        self.assertEqual(budget["required_runtime_reserve_bytes"], 4096)
        self.assertEqual(budget["required_linker_heap_bytes"], 45_644)

    def test_iis_allocation_is_derived_from_driver_operands(self):
        driver = '''define i32 @iis_open(i8* %pd, i32 %cbuf) {
  %ch32 = zext i8 %ch_num.0 to i32
  %bytes = shl nuw nsw i32 %ch32, 3
  %samples = load i16, i16* %sr_points.ptr, align 2
  %sample32 = zext i16 %samples to i32
  %size = mul nuw nsw i32 %bytes, %sample32
  %buffer = call i8* @malloc(i32 %size)
  ret i32 0
}
'''
        self.assertEqual(iis_heap_allocations(driver)["total_requested_bytes"], 1024)
        for changed in (driver.replace(", 3", ", 2"),
                        driver.replace("zext i16 %samples", "zext i8 %samples"),
                        driver.replace("%ch_num.0", "%unreviewed_channel"),
                        driver.replace("ret i32 0", "%second = call i8* @malloc(i32 %size)\n  ret i32 0")):
            with self.assertRaises(ValueError):
                iis_heap_allocations(changed)

    def test_usb_allocation_measurement(self):
        cdc = 'define void @cdc_register(i8 %id) {\n call i8* @zalloc(i32 200)\n call i8* @malloc(i32 64)\n}\n'
        config = 'define i32 @usb_config(i8 %id) {\n call i8* @zalloc(i32 972)\n}\n'
        self.assertEqual(usb_heap_allocations(cdc, config)["total_requested_bytes"], 1236)
        with self.assertRaises(ValueError):
            usb_heap_allocations(cdc, config.replace("972", "1024"))


if __name__ == "__main__":
    unittest.main()
