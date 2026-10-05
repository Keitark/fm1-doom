import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_target_candidate import (iis_heap_allocations, task_heap_budget,
                                    usb_heap_allocations, verify_sdfilesystem)


class TargetBudgetTests(unittest.TestCase):
    def test_reviewed_task_budget(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["minimum_task_heap_bytes"], 37_464)
        self.assertEqual(budget["required_linker_heap_bytes"], 43_596)

    def test_old_stack_exceeds_previous_linker_heap(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        source = source.replace('"fm1_doom", 10, 2048', '"fm1_doom", 10, 8192')
        budget = task_heap_budget(source, 1236)
        self.assertEqual(budget["required_linker_heap_bytes"], 68_172)
        self.assertGreater(budget["required_linker_heap_bytes"], 48_332)

    def test_audio_dma_is_reserved_before_runtime_margin(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        budget = task_heap_budget(source, 1236, 1024)
        self.assertEqual(budget["minimum_task_heap_bytes"], 37_464)
        self.assertEqual(budget["audio_dynamic_heap_bytes"], 1024)
        self.assertEqual(budget["required_runtime_reserve_bytes"], 4096)
        self.assertEqual(budget["required_linker_heap_bytes"], 44_620)

    def test_usb_reduction_returns_heap_without_changing_doom_stack(self):
        source = (ROOT / "src/fm1_doom_target.c").read_text(encoding="utf-8")
        previous = source.replace('"doom_usb", 11, 768', '"doom_usb", 11, 1024')
        current = task_heap_budget(source, 1236, 1024)
        old = task_heap_budget(previous, 1236, 1024)
        self.assertEqual(old["minimum_task_heap_bytes"] - current["minimum_task_heap_bytes"], 1024)
        doom = next(task for task in current["tasks"] if task["name"] == "fm1_doom")
        self.assertEqual(doom["stack_bytes"], 8192)

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

    def test_sdfilesystem_keeps_stock_configuration_drivers(self):
        nm = "\n".join((
            "02000120 R _vfs_ops_begin", "02000120 R sdfile_vfs_ops",
            "02000198 R nor_sdfile_vfs_ops", "02000210 R sdfile_ext_vfs_ops",
            "02000288 R _vfs_ops_end"))
        self.assertEqual(verify_sdfilesystem(nm)["registration_bytes"], 360)
        for changed in (nm.replace("sdfile_ext_vfs_ops", "missing_driver"),
                        nm.replace("02000288", "02000300"),
                        nm.replace("02000198", "02000199"),
                        nm + "\n02000400 R fat_vfs_ops"):
            with self.assertRaises(ValueError):
                verify_sdfilesystem(changed)


if __name__ == "__main__":
    unittest.main()
