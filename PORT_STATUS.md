# Port status and acceptance gates

## Evidence from this branch

| Check | Result | Limit |
| --- | --- | --- |
| MSVC Release build | Pass, Doomgeneric plus FM-1 video/input adapter | Host only |
| `ctest` port contract | Pass, 240×240 palette/strip endpoints and simultaneous key edges | Mock callbacks |
| Freedoom Phase 1 smoke, 120 engine ticks, 2 MiB zone | Pass; 9,720 four-row strip callbacks in the initial adapter; nonflat E1M1 gameplay image | Host file-backed WAD, no sound |
| Same smoke, 1 MiB zone | Fail: `Z_Malloc` requested 54,912 more bytes | Shows this engine layout exceeds FM-1 RAM |
| Doom shareware 1.8, E1M1 only, 4×4 graphics, silent marker | 829,479-byte WAD; 268,485-byte FMD1 with 4 KiB blocks/cache | No full-level playthrough |
| Selected 8×8 graphics, silent marker | 729,773-byte WAD; 191,534-byte FMD1 with 16 KiB blocks/cache | Recognizable boot image; no full-level playthrough |
| Same E1M1, 16×16 graphics, silent marker | 679,817-byte WAD; 174,114-byte FMD1 with 16 KiB blocks/cache | Recognizable boot image; no full-level playthrough |
| FMD1 direct host read, 120 engine ticks | Pass; exact image hash match with unpacked WAD | Host has 2 MiB zone and zlib DLL |
| Reduced E1M1 host zone diagnostic | 768 KiB passes 120 ticks for 4×4, 8×8, and 16×16 assets; 640 and 512 KiB fail a 64,040-byte allocation with 4×4 assets | Does not include SDK/static RAM or a complete playthrough |
| FMD1 Python and C tests | Pass; roundtrip, block crossing, cache, bounds, CRC | C unit test uses an injected decoder |
| pi32v2 compile of six port/archive sources | Pass with pinned AC79 SDK headers/toolchain | No Doom core, zliblite link, or SDK link |
| pi32v2 compile of 79 original engine files | Pass with pinned SDK toolchain | Compile only |
| Offline SDK+Doom size probe, adapter rooted | Links; app sections 434,384 B without WAD; `.ram0_data` 68,048 B and `.ram0_bss` 342,080 B | Retains NES app and uses nonfunctional libc shims; not runnable Doom firmware |
| Current eight-row host output | 4,860 callbacks over 120 ticks; exact image hash match with four-row output | No hardware timing or FPS result |
| FM-1 boot, LCD, keys, audio | Not attempted | No installable image |

The first smoke used Freedoom 0.13.0 `freedoom1.wad` from the project's official
[release](https://github.com/freedoom/freedoom/releases/tag/v0.13.0), stored
locally outside Git. The screenshot stays in ignored `build/`. This is an IWAD
compatibility test of the original engine, not a performance measurement.
The E1M1 data test used a separately obtained Doom shareware 1.8 IWAD; none of
that input, the derived WAD, or the FMD1 archive is committed.
The host-only `FM1_DOOM_ZONE_KIB=768` setting overrides Doom's `-mb` minimum
to test smaller zones. The successful 768 KiB run still greatly exceeds the
FM-1's entire linkable RAM once SDK/static allocations are considered.
The size probe is reproducible with `python tools/compile_target_engine.py
--fm1-root F:\dev\fm1` and `python tools/link_target_probe.py --fm1-root
F:\dev\fm1` after the port compile. It needs the existing reviewed offline NES
boot build as a baseline and never emits an update package.

## Hardware boundary

The established FM-1 board has a 240×240 LCD, 41 scanner slots, an AC7911B8,
about 523 KiB linkable SRAM before SDK use, and 1 MiB internal flash. The
current tracker v31 candidate uses 381,192 bytes of `.ram0_bss` and leaves a
131,948 byte heap according to its offline static audit; those figures describe
that image, not this Doom port. Doomgeneric's host allocator is not a target
memory plan. Its normal game-data path uses `fopen`/`fseek` on an IWAD.

The [RP2040 Doom port](https://github.com/kilograham/rp2040-doom) demonstrates
that a reworked original engine can fit 264 KiB of RAM by keeping immutable
level data in flash. Its [memory notes](https://kilograham.github.io/rp2040-doom/speed_and_ram.html)
describe why a straight Chocolate Doom build does not fit. The FM-1 still needs
an engine memory redesign. FMD1 now supplies bounded random reads of the
selected 8×8 E1M1 WAD in 191,534 bytes with a 16 KiB cache, but the reader is
not bound to the physical flash map. The current stock V15 application
allocation is 602,112 bytes, and the
tracker v31 application occupies 340,912 bytes; an E1M1 archive plus the full
Doom engine cannot be declared to fit from those figures. The corrected probe's
434,384 B app sections plus the selected 8×8 archive total 625,918 B before
any new integration code, 23,806 B above the stock V15 app allocation. Its
`.ram0_data` and `.ram0_bss` total 410,128 B before a Doom zone or task stack.
Extending the app
region would require a reviewed layout that preserves protected identity data
and a rollback path.

## Required before calling this playable on FM-1

1. Place the reduced FMD1 archive in a reviewed flash layout and prove bounded
   random reads on the physical device, including corruption/error handling.
2. Replace or rework the engine's 2 MiB host zone and cache/level layout;
   replace probe-only libc shims and the NES baseline with a real Doom app,
   then prove SRAM, stack, heap, and protected-flash budgets from the ELF and
   image audit.
3. Wire the existing board LCD, scanner, timer, USB/storage, and DAC services
   to the callbacks in this repo; add responsive audio without ISR blocking.
4. Reproduce the host gameplay image through the LCD, then measure controls,
   frame rate, WAD latency, audio, battery behavior, and restart on the bench.
5. Preserve the known-good installed v30 image, verify a candidate and restore
   plan, and perform any physical write only as a deliberate bench step.

Until these gates pass, this repository contains source and an offline host
preview only. No file here is a firmware update payload.
