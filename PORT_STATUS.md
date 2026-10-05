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
| Experimental 160×100 engine, selected 8×8 E1M1 | 448 KiB zone passes 120 host ticks with compact HUD; 384 KiB fails allocation | Generated source copy, short idle run, no FM-1 execution |
| Direct-E1M1 no-UI stage, 8×8 assets | 361,801 B raw; 79,785 B FMD1 with 4 KiB blocks/cache | Menus, text HUD and completion screens omitted |
| Direct-E1M1 host smoke | 32-bit host passes 120 idle ticks and 300 moving ticks with a 296 KiB zone; 4 KiB FMD1 produces the original final image hash on 64-bit host | No full-level traversal, hardware frame timing or audio |
| FMD1 Python and C tests | Pass; roundtrip, block crossing, cache, bounds, CRC | C unit test uses an injected decoder |
| pi32v2 compile of six port/archive sources | Pass with pinned AC79 SDK headers/toolchain | No Doom core, zliblite link, or SDK link |
| pi32v2 compile of 79 original engine files | Pass with pinned SDK toolchain | Compile only |
| Offline SDK+Doom size probe, adapter rooted | Links; app sections 434,384 B without WAD; `.ram0_data` 68,048 B and `.ram0_bss` 342,080 B | Retains NES app and uses nonfunctional libc shims; not runnable Doom firmware |
| Experimental 160×100 size probe | Links; 433,296 B app sections before WAD, 343,088 B static RAM | Same probe limitations; selected archive makes 624,830 B before integration |
| Direct-E1M1 Doom-only size probe | Links without NES app; 363,888 B app sections before WAD; fixed 296 KiB zone and 4 KiB cache included in 477,656 B static RAM; 45,900 B linked heap span | Empty `app_main`, nonfunctional libc shims; not bootable Doom |
| Current eight-row host output | 4,860 callbacks over 120 ticks; exact image hash match with four-row output | No hardware timing or FPS result |
| FM-1 boot, LCD, keys, audio | Not attempted | No installable image |

The first smoke used Freedoom 0.13.0 `freedoom1.wad` from the project's official
[release](https://github.com/freedoom/freedoom/releases/tag/v0.13.0), stored
locally outside Git. The screenshot stays in ignored `build/`. This is an IWAD
compatibility test of the original engine, not a performance measurement.
The E1M1 data test used a separately obtained Doom shareware 1.8 IWAD; none of
that input, the derived WAD, or the FMD1 archive is committed.
The host-only `FM1_DOOM_ZONE_KIB` setting overrides Doom's `-mb` minimum.
The 296 KiB result applies only to the generated, no-UI 32-bit build. That
build puts the state and actor definition tables in flash and bounds the
renderer to 64 visplanes. The older 768 KiB result applies to the normal
320×200 engine. These different profiles should not be mixed.
The size probe is reproducible with `python tools/compile_target_engine.py
--fm1-root F:\dev\fm1` and `python tools/link_target_probe.py --fm1-root
F:\dev\fm1` after the port compile. It needs the existing reviewed offline NES
boot build as a baseline and never emits an update package.

## Hardware boundary

The established FM-1 board has a 240×240 LCD, 41 scanner slots, an AC7911B8,
523,596 B linkable `ram0` before SDK use, and 1 MiB internal flash. The
current tracker v31 candidate uses 381,192 bytes of `.ram0_bss` and leaves a
131,948 byte heap according to its offline static audit; those figures describe
that image, not this Doom port. Doomgeneric's host allocator is not a target
memory plan. Its normal game-data path uses `fopen`/`fseek` on an IWAD.

The [RP2040 Doom port](https://github.com/kilograham/rp2040-doom) demonstrates
that a reworked original engine can fit 264 KiB of RAM by keeping immutable
level data in flash. Its [memory notes](https://kilograham.github.io/rp2040-doom/speed_and_ram.html)
describe why a straight Chocolate Doom build does not fit. The current
direct-E1M1 FMD1 archive is 79,785 B with a 4 KiB cache; the reader has not
been bound to physical flash. The Doom-only probe's 363,888 B app sections
plus this archive total 443,673 B, below the stock V15 602,112 B app
allocation. Its fixed 296 KiB zone and 4 KiB archive cache are already in
the 477,656 B static RAM count. The linker reports a 45,900 B heap span.
This does not prove a real build fits: task stacks, SDK heap, zliblite scratch,
board services, and protected flash layout are still missing. The probe's
empty `app_main` and libc shims prevent Doom from running.

## Required before calling this playable on FM-1

1. Place the reduced FMD1 archive in a reviewed flash layout and prove bounded
   random reads on the physical device, including corruption/error handling.
2. Exercise the linked fixed zone under a real SDK Doom task; replace
   probe-only libc shims and the inert app root,
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
