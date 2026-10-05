# Offline FM-1 Doom application candidate

The current low-memory E1M1 build has a real FM-1 SDK `app_main`. It creates a
separate Doom task so SDK event dispatch continues. The task validates a local
FMD1 archive linked in XIP flash, initializes the recovered stock LCD sequence,
starts the SPI2 key scanner, and runs Doom. LCD rows use the 240×240 RGB565
strip interface at the stock-derived row origin 40. Scanner bits have a 10 ms
stability filter. Detected initialization and scanner faults stop those
peripherals and leave a RAM fault code; an LCD transfer fault enters Doom's
engine error path. Both paths need physical fault testing.

The first-stage game contains E1M1 only. Its original image menu offers New
Game and graphic detail; completing E1M1 starts E1M1 again rather than entering
an absent intermission/E1M2. Music and sound effects, save/load, and persistent
configuration are disabled. `fm1_doom_frames`, `fm1_doom_last_frame_ms`,
`fm1_doom_max_frame_interval_ms`, `fm1_doom_slow_frames`, `fm1_doom_stage`, and
`fm1_doom_fault` are RAM diagnostics. They are not a completed 30 FPS test.

## Reproduce without touching the device

Use a lawful local Doom shareware IWAD. The commands create only ignored local
artifacts; no game data is committed.

```powershell
git submodule update --init
python tools/make_lowres_engine.py
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 8
python tools/pack_archive.py build\stage-menu-ui.wad build\menu-ui-4k.wad --block-size 4096
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/build_target_candidate.py build\menu-ui-4k.wad --fm1-root F:\dev\fm1
```

The builder requires the pinned SDK and a reviewed local FM-1 boot baseline.
It verifies every FMD1 block and CRC, checks that the exact archive bytes land
in the ELF's read-only `.text` and extracted app image, then enforces the stock
602,112 B app allocation and 523,596 B RAM0 limit. It compiles the current
board root and LCD/key sources; unchanged baseline boot sources must still
match their recorded hashes. Outputs are
`build/target-candidate/fm1-doom-candidate.elf`,
`build/target-candidate/fm1-doom-candidate.app.bin`, and
`build/target-candidate/build-manifest.json`. The raw `.app.bin` is **not** an
update package and must not be flashed as one.

For the locally tested shareware 1.8 archive (117,590 B), the candidate links
at 482,448 B app bytes. `.ram0_data` is 28,144 B and `.ram0_bss` is 447,080 B.
The linker heap span is 48,332 B before the 8 KiB Doom task stack, runtime
allocations, and zliblite scratch. The archive is included in those app bytes,
not added afterward. The ELF/app bytes and archive hashes are in the local
manifest. This proves an offline link and static placement, not boot or runtime
memory safety.

## Validation and remaining gates

- The 32-bit Windows host completes E1M1 restart and menu actions with a
  296 KiB Doom zone. A virtual-IWAD host run starts from an FMD1 archive even
  when no `doom1.wad` file exists on the filesystem.
- Host tests check LCD column/row commands, the stock row-40 offset, complete
  eight-row RGB565 transfer, failure propagation, and key debounce.
- The actual FM-1 must still verify startup, power handoff, XIP reads,
  decoder heap use, task stack, LCD/key behavior, and the full ELF/update
  layout. Sound, USB recovery, and 30 completed distinct gameplay frames/s
  remain unimplemented or unmeasured.
- Preserve the known-good installed image and a reviewed restore path before
  any physical write. No device operation is performed by the builder.
