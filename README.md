# FM-1 Doom

Port work for the **original Doom engine** on the M-VAVE FM-1. The engine is
[Doomgeneric](https://github.com/ozkl/doomgeneric), pinned as a GPL-2.0
submodule at `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`. Game assets are
separate: bring a lawful IWAD such as the Doom shareware WAD or Freedoom. No
WAD, stock firmware dump, device key, or flashable image is distributed here.

**Status: compressed E1M1 host milestone, not a playable FM-1 firmware.** The engine
renders through a 320×200 indexed buffer into FM-1-sized 240×240 RGB565 strips,
and the stock 41-slot key scanner is mapped to Doom key edges. A Windows host
runner produced real gameplay frames with Freedoom Phase 1 and with an aggressively
reduced Doom shareware E1M1 archive. The target adapter and archive reader
compile for pi32v2. The engine has not been linked to
the FM-1 SDK, connected to the physical LCD/key scanner, or run on the device.
See [PORT_STATUS.md](PORT_STATUS.md) and [issue #1](https://github.com/Keitark/fm1-doom/issues/1).

## Build and test on Windows

```powershell
git submodule update --init
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\fm1_doom_host.exe C:\path\to\your\IWAD.WAD 120 build\frame.ppm 2
```

Arguments are IWAD path, number of engine ticks, output PPM path, and optional
Doom zone size in MiB (default 6). The runner starts episode 1, map 1 without
input and disables music and sound. The PPM shows exactly what the FM-1-sized
video adapter emitted. It is a host rig; it does not send pixels to the device.

## Make a small E1M1 archive

Bring a lawful Doom shareware IWAD. The following creates a local staged WAD
containing E1M1, needed graphics and sprites, and coarse 8×8 asset pixels.
The stage tool does not include game data in this repository. With `--silent`,
the engine's still-referenced E1M1 music lump becomes a zero-length marker;
this saves 17,283 bytes of raw data and about 17 KiB of host zone use. These options
target direct E1M1 boot with sound disabled; other levels, menus, and completion
screens are not yet validated.

```powershell
python tools/stage_wad.py C:\path\to\doom1.wad build\stage.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --pixelate 8
python tools/pack_archive.py build\stage.wad build\doom1.wad --block-size 16384
```

`build\doom1.wad` is an **FMD1 compressed archive**, despite its name. Doom's
IWAD discovery requires the `doom1.wad` filename; our WAD reader detects and
decodes FMD1 blocks. The Windows runner needs `zlib1.dll` on `PATH`, such as
the one installed with Git for Windows. Its image is not an ordinary WAD for
other Doom ports.

```powershell
.\build\Release\fm1_doom_host.exe build\doom1.wad 120 build\e1m1.ppm 2
python tools/pack_archive.py build\doom1.wad build\restored.wad --unpack
```

On the tested shareware 1.8 input, asset pixelation gave these sizes:

| Pixel blocks | Staged WAD | FMD1, 4 KiB cache | FMD1, 16 KiB cache |
| ---: | ---: | ---: | ---: |
| 4×4 | 829,479 B | 268,485 B | 234,158 B |
| **8×8 (selected)** | **729,773 B** | **221,921 B** | **191,534 B** |
| 16×16 | 679,817 B | 203,100 B | 174,114 B |

The selected 8×8 direct compressed run rendered a recognizable start view at 768 KiB
of host Doom zone. Its 4 KiB and 16 KiB archive variants produced the same
120-tick 240×240 image hash. These are local measurements for that input, not a
guarantee for every IWAD or a complete level playthrough.
For a host-only memory check, set `FM1_DOOM_ZONE_KIB` before running the host
binary. The reduced E1M1 booted at 768 KiB; 640 KiB failed a 64,040-byte
allocation. This test does not account for SDK, stack, or screen RAM.

With the clean pinned SDK and toolchain from the existing
[FM-1 board project](https://github.com/Keitark/fm1-tracker), check pi32v2
compilation of the port only:

```powershell
python tools/compile_target_port.py --fm1-root F:\dev\fm1
```

This never packages or flashes firmware. It does not compile the full Doom
engine for the target or link zliblite into a firmware image. For deeper
diagnostics, `tools/compile_target_engine.py` compiles the 79 engine files and
`tools/link_target_probe.py` measures an offline SDK link. The probe retains
the existing NES boot app and uses deliberately nonfunctional file-operation
shims; it is never a firmware candidate.

## Port interfaces

- `src/i_video_fm1.c` keeps the Doom renderer's indexed 320×200 framebuffer and
  converts eight rows at a time. The caller consumes big-endian RGB565
  pixels before each callback returns.
- `src/fm1_doom_port.c` maps the stock scanner's measured slots to Doom keys and
  delivers all simultaneous press/release edges before polling again.
- `src/doomgeneric_fm1.c` binds those callbacks to Doomgeneric's timing and
  input hooks.
- `src/fm1_doom_archive.c` validates FMD1 block locations and CRCs, and reads
  arbitrary virtual WAD byte ranges through one decoded-block cache.
- `src/w_file_fm1.c` connects that reader to the original engine's WAD file
  interface. `src/fm1_fmd_zliblite.c` calls the SDK's zliblite decoder.

| Stock slot | Doom action |
| ---: | --- |
| 14, 17, 16, 18 | Left, forward, backward, right |
| 40, 38 | Fire, use |
| 15, 20 | Run, strafe modifier |
| 19, 22 | Menu, enter |
| 21, 23 | Weapons 1, 2 |

The slot assignments use the recovered FM-1 scanner table from the board
project. They still need gameplay acceptance on the physical key matrix.

## Why this is not ready to install

The FM-1 has 1 MiB of internal flash and roughly 500 KiB of application RAM.
The compressed E1M1 data itself can fit in that flash. A corrected offline link
probe that retains the FM-1 input/video adapter measured 434,384 bytes of app
sections without assets. With the selected 191,534-byte archive this totals
625,918 bytes, 23,806 bytes above the stock 602,112-byte app allocation. The
probe retains NES and nonfunctional libc shims;
this arithmetic does not prove a real Doom firmware will fit.
On the host, a full Freedoom WAD needed more than a 1 MiB zone. The reduced
E1M1 passed 768 KiB but failed at 640 KiB. A lower-memory engine architecture
and a verified flash layout are required before a standalone device build is
credible. Audio, SDK task integration, image packaging, rollback, and bench
acceptance remain open. See the exact gates in [PORT_STATUS.md](PORT_STATUS.md).
The hardware goal is 30 completed LCD gameplay frames/s; see
[PERFORMANCE_TARGET.md](PERFORMANCE_TARGET.md) for the measurement contract.
