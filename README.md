# FM-1 Doom

Port work for the **original Doom engine** on the M-VAVE FM-1. The engine is
[Doomgeneric](https://github.com/ozkl/doomgeneric), pinned as a GPL-2.0
submodule at `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`. Game assets are
separate: bring a lawful IWAD such as the Doom shareware WAD or Freedoom. No
WAD, stock firmware dump, device key, or flashable image is distributed here.

**Status: Doom is flashed and gameplay is visible on the FM-1.** The engine
renders through a 160×100 indexed buffer into FM-1-sized 240×240 RGB565 strips,
and the stock 41-slot key scanner is mapped to Doom key edges. A Windows host
runner produced real gameplay frames with Freedoom Phase 1 and with an aggressively
reduced Doom shareware E1M1 archive. A real SDK Doom task, XIP-embedded
compressed archive, LCD strip output, and SPI2 key scanner now link for pi32v2.
The corrected startup uses the working NES paced key scanner, row-zero LCD
placement and USB fault reporting/recovery. The SDK's missing `%i` formatter
support was the confirmed engine startup fault. Serial frame counters advance
with zero engine/LCD/key errors, and the user confirms visible gameplay.
Movement, menu and firing worked after a power cycle, but input subsequently
stalled with scanner error `-3` while frames kept advancing. The revised build
uses a hardware-interpolated scanner clock and continuing recovery with backoff.
The user now confirms stable controls and both presentation modes; a 60-second
Detailed-mode capture has zero faults and about 11 counter increments/s.
The installed audio backend plays original pistol/pickup/oof effects and the
E1M1 score through an eight-voice synthesizer; the user confirms hearing music
and effects. A 105-second Detailed-mode capture has zero reported errors,
successful music looping and about 10.9 counter increments/s. Instrument
quality and mix balance are being checked; the 30 FPS target remains open.
See [audio](AUDIO.md).
The next source revision adds the PB6 master volume knob, lossless original
PCM effects, DOS OPL2 music and an E4 switch for the requested VCO/VCF/VCA
melody treatment. Effects and synth-mode drums are 6 dB quieter. Its local
app is 554,800 B with 477,912 B static RAM and 4,096 B of reviewed startup
reserve; physical installation and timing checks are pending.
USB position/frame diagnostics can inspect the device display with
`python tools/capture_usb_frame.py --port COM6` after that revision is installed.
The private `build/target-candidate/app.bin` is the verified plain input for a
WL82 UBOOT flow accepting SDK `-app app.bin`; see [target candidate](TARGET_CANDIDATE.md)
for its exact format and limits.
See [deployment evidence](DEPLOYMENT.md) for the verified image and hardware result.
See [PORT_STATUS.md](PORT_STATUS.md) and [issue #1](https://github.com/Keitark/fm1-doom/issues/1).

The current 160×100 direct-E1M1 build has two presentation modes: Smooth uses
exact 8×8 LCD blocks; Detailed uses all 160×93 gameplay samples. Both fill the
240×240 display, and the original image menu and compact HUD stay detailed.
Press D♯4 (the fifth black key from the left) to toggle while playing.
The audio build uses original pistol art, 2×2 fist art and 4×4 world/enemy assets:
177,331 B with a 4 KiB decode cache. The graphics/input milestone's full-detail
fist archive was 181,995 B. Both modes pass 32-bit host gameplay
checks at a 296 KiB Doom zone. Whole-texture caches now use a bounded column
buffer, preventing the reproduced fragmented-zone allocation failure. Runtime
stack/heap and whole-level hardware acceptance remain open. See the
[audit](AUDIT.md),
[target candidate](TARGET_CANDIDATE.md) and [low-memory profile](LOW_MEMORY_EXPERIMENT.md).

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
target direct E1M1 boot with sound disabled; other levels and completion
screens are not yet validated. The low-memory menu profile below is the
current first-stage build.

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
| 8×8 (initial profile) | 729,773 B | 221,921 B | 191,534 B |
| 16×16 | 679,817 B | 203,100 B | 174,114 B |

The selected 8×8 direct compressed run rendered a recognizable start view at 768 KiB
of host Doom zone. Its 4 KiB and 16 KiB archive variants produced the same
120-tick 240×240 image hash. These are local measurements for that input, not a
guarantee for every IWAD or a complete level playthrough.
For a host-only memory check, set `FM1_DOOM_ZONE_KIB` before running the host
binary. The reduced E1M1 booted at 768 KiB; 640 KiB failed a 64,040-byte
allocation. This test does not account for SDK, stack, or screen RAM.

For the current direct E1M1 first stage, use the generated 160×100 engine:

```powershell
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 4 --weapon-pixelate 1 --fist-pixelate 2 --compact-assets
python tools/pack_archive.py build\stage-menu-ui.wad build\menu-ui-4k.fmd --block-size 4096
```

This retains original menu images and pistol art while reducing the four fist
frames to leave room for audio. Lossless patch-column/payload sharing and
texture patch-index compaction preserve decoded graphics and all map lumps.
The resulting WAD is 552,294 B and its 4 KiB FMD1 archive is 170,505 B on the
tested input. Without `--compact-assets`, the prior 582,735 B/177,331 B profile
is reproduced.
Generate the separate sound/music banks with the commands in [AUDIO.md](AUDIO.md)
before building the audio target. The zero-length music marker remains in the
WAD; playback uses the separate immutable score.
`LOW_MEMORY_EXPERIMENT.md` has the exact build and run commands.

### Leaving the spawn area

The complete original E1M1 geometry is retained. Walking straight from spawn
reaches a normal wall; the opening is to the right. Turn right with A3, move
with G-sharp3, then turn left with F3 into the corridor. Turn right farther
along toward the first door and press F5 once nearby. Release F5 before using
another door. Holding USE continuously does not activate later doors, and
repeated presses can reverse an opening door. D-sharp4 enables the clearer
view. A 296 KiB host route crosses the first door and reaches active enemies;
whole-level physical traversal remains unverified.

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

| Physical note | Stock slot | Doom action |
| --- | ---: | --- |
| F3 / G♯3 / G3 / A3 | 14 / 17 / 16 / 18 | Left / forward / backward / right |
| G5 / F5 (far-right white keys) | 40 / 38 | Fire / use |
| F♯3 / B3 | 15 / 20 | Run / strafe modifier |
| A♯3 / C♯4 | 19 / 22 | Menu/back / enter; fire also selects in the menu |
| C4 / D4 | 21 / 23 | Weapons 1 / 2 |
| D♯4 (fifth black key from left) | 24 | Toggle Smooth / Detailed |

The slot assignments use the recovered FM-1 scanner table from the board
project. These are fixed physical-note names; MIDI octave/transposition settings
do not change the Doom controls. Movement/fire and both views are confirmed
working after the scanner fix; whole-level input acceptance remains open.

## Limits of the UBOOT input

The reviewed V14/v32 app slot is 584,956 B. The installed E1M1 audio app,
including its compressed archive and audio banks, is 583,216 B, leaving 1,740 B.
It includes a fixed 296 KiB Doom zone, 4 KiB archive cache and 7 KiB bounded
inflater arena. Static RAM is 475,176 B, leaving a 48,364 B linked heap span.
Reviewed startup uses 38,488 B for tasks/queues/idle, an 800 B initialization
allowance, 1,236 B USB requests and 1,024 B IIS DMA, leaving 6,816 B before
other allocations/padding. Live USB stack telemetry retains 1,512 B unused.
A full Freedoom WAD still needs far more memory. Runtime heap/Doom-stack
measurement, whole-map traversal, audio quality and the requested 30 FPS remain
open.
See the exact gates in [PORT_STATUS.md](PORT_STATUS.md).
The hardware goal is 30 completed LCD gameplay frames/s; see
[PERFORMANCE_TARGET.md](PERFORMANCE_TARGET.md) for the measurement contract.
