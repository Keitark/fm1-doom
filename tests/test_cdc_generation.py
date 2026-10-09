"""Run production CDC task generation and bounded reply queue regressions."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class OverlayWriter:
    @staticmethod
    def cdc(text):
        return text

    @staticmethod
    def function(text, name, replacement):
        if name != "cdc_write_data":
            raise AssertionError("Unexpected overlay writer replacement")
        return replacement


class CdcGenerationRegression(unittest.TestCase):
    def test_actual_task_cancels_copied_old_reply_when_ready_stays_true(self):
        spec = importlib.util.spec_from_file_location("doom_usb_overlay", ROOT / "tools/doom_usb_overlay.py")
        overlay = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(overlay)
        self.assertIsNotNone(shutil.which("cmake"), "CMake/compiler required for production-path regression")
        with tempfile.TemporaryDirectory(prefix="doom-cdc-generation-") as directory:
            fixture = Path(directory)
            (fixture / "cdc_overlay_generated.inc").write_text(overlay.cdc("", OverlayWriter()), encoding="utf-8")
            (fixture / "cdc_task_sdk_fake.h").write_text("""#ifndef CDC_TASK_SDK_FAKE_H
#define CDC_TASK_SDK_FAKE_H
#include "packet_fake.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define FM1_USB_CONTROLLER 0u
#define CDC_CLASS 1u
int usb_device_mode(usb_dev,unsigned);
uint32_t timer_get_ms(void);
u32 cdc_read_data(usb_dev,u8 *,u32);
void os_time_dly(unsigned);
unsigned fm1_usb_rx_generation(void);
int fm1_usb_rx_fault(void);
int fm1_usb_boot_pending(void);
int fm1_usb_boot_arm(void);
void fm1_doom_sound_set_speaker_muted(unsigned);
unsigned fm1_doom_sound_lock(void);
void fm1_doom_sound_unlock(unsigned);
#endif
""", encoding="utf-8")
            for name in ("app_config.h", "system/includes.h", "system/sys_time.h", "os/os_api.h",
                         "usb/device/cdc.h", "usb/usb_config.h", "boot_entry.h", "fm1_doom_sound.h"):
                header = fixture / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "cdc_task_sdk_fake.h"\n', encoding="utf-8")
            # This build description exists only inside TemporaryDirectory;
            # the repository CMake and firmware files are never rewritten.
            (fixture / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 3.20)
project(cdc_generation_regression C)
set(CMAKE_C_STANDARD 99)
add_executable(cdc_generation_tests "{ROOT.as_posix()}/tests/cdc_generation_task_tests.c"
  "{ROOT.as_posix()}/src/fm1_usb_packet.c" "{ROOT.as_posix()}/src/fm1_doom_usb_protocol.c")
target_include_directories(cdc_generation_tests PRIVATE "{fixture.as_posix()}"
  "{ROOT.as_posix()}/include" "{ROOT.as_posix()}/tests")
target_compile_definitions(cdc_generation_tests PRIVATE FM1_USB_PACKET_HOST=1 _CRT_SECURE_NO_WARNINGS)
""", encoding="utf-8")
            build = fixture / "build"
            for command in (["cmake", "-S", str(fixture), "-B", str(build)],
                            ["cmake", "--build", str(build), "--config", "Release"]):
                result = subprocess.run(command, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace", timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            executable = build / ("Release/cdc_generation_tests.exe" if os.name == "nt" else "cdc_generation_tests")
            result = subprocess.run([str(executable)], capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS CDC captured generation", result.stdout)
            self.assertIn("AUDIO reply=511", result.stdout)
            self.assertIn("VOLUME reply=", result.stdout)
            self.assertIn("USB_AUDIO reply=447", result.stdout)
            self.assertEqual(result.stdout.count("duplicate burst drops one whole reply"), 3)


if __name__ == "__main__":
    unittest.main()
