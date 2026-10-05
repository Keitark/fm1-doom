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
compile for pi32v2. Diagnostic SDK links can omit the NES app but still use
nonfunctional file shims; a real Doom application has not been linked, connected to the
physical LCD/key scanner, or run on the device.
See [PORT_STATUS.md](PORT_STATUS.md) and [issue #1](https://github.com/Keitark/fm1-doom/issues/1).

An experimental 160×100 direct-E1M1 build renders the selected 8×8 assets
with a small health/ammo HUD. Its no-UI archive is 79,785 B with a 4 KiB
decode cache; a 32-bit host completed 300 moving ticks with a 296 KiB Doom
zone. The offline size arithmetic now fits the stock flash allocation and
nominal SRAM, but excludes the real SDK task, stack, decoder scratch, and board
bindings. See the [reproduction and limits](LOW_MEMORY_EXPERIMENT.md).

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

For a direct E1M1 first stage, add `--no-ui` to the stage command and use the
generated 160×100 engine. This also removes menu, intermission, status, and
text HUD graphics; it cannot support level completion or menus. The resulting
WAD is 361,801 B and its 4 KiB block FMD1 archive is 79,785 B on the tested
shareware input. `LOW_MEMORY_EXPERIMENT.md` has the exact build and run commands.

With the clean pinned SDK and toolchain from the existing
[FM-1 board project](https://github.com/Keitark/fm1-tracker), check pi32v2
compilation of the port only:

```powershell
python tools/compile_target_port.py --fm1-root F:\dev\fm1
```

This never packages or flashes firmware. It does not compile the full Doom
engine for the target or link zliblite into a firmware image. For deeper
diagnostics, `tools/compile_target_engine.py` compiles the 79 engine files and
`tools/link_target_probe.py` measures an offline SDK link. The `--doom-only`
option replaces the NES app with an inert probe root. Both modes use
deliberately nonfunctional file-operation shims and are never firmware candidates.

## Port interfaces

- `src/i_video_fm1.c` consumes the Doom renderer's indexed framebuffer (320×200
  normally, 160×100 in the direct-E1M1 build) and converts eight LCD rows at a
  time. The caller consumes big-endian RGB565
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
| 19, 22 | Menu, enter (slot 19 disabled in the no-UI build) |
| 21, 23 | Weapons 1, 2 |

The slot assignments use the recovered FM-1 scanner table from the board
project. They still need gameplay acceptance on the physical key matrix.

## Why this is not ready to install

The stock V15 app allocation is 602,112 B. The direct-E1M1 Doom-only size
probe has 363,888 B of app sections before data; adding the 79,785 B archive
gives 443,673 B. It links a fixed 296 KiB target zone and 4 KiB archive cache
with 477,656 B total static RAM, leaving a 45,900 B linked heap span. The
probe's `app_main`
does not start Doom and its file shims are nonfunctional. Stacks, SDK heap,
decompressor scratch, flash placement, and real board services are not yet
accounted for. A full Freedoom WAD still needs far more memory. Audio, SDK
task integration, image packaging, rollback, and bench acceptance remain open.
See the exact gates in [PORT_STATUS.md](PORT_STATUS.md).
The hardware goal is 30 completed LCD gameplay frames/s; see
[PERFORMANCE_TARGET.md](PERFORMANCE_TARGET.md) for the measurement contract.
