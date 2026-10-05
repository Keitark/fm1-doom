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
| Direct-E1M1 menu stage, 8×8 assets | 465,581 B raw; 117,590 B FMD1 with 4 KiB blocks/cache | Original menu patches retained, half-size menu rendered; no game data committed |
| Direct-E1M1 host smoke | 32-bit host passes 120 idle ticks and 300 moving ticks with a 296 KiB zone; 4 KiB FMD1 produces the original final image hash on 64-bit host | No full-level traversal, hardware frame timing or audio |
| Menu and 8×8 LCD host smoke | 32-bit host passes 80 ticks each for open, options, resume, and New Game restart at a 296 KiB zone; menu art and exact 8×8 gameplay blocks inspected in 240×240 output | Host only; menu image uses finer scaling |
| FMD1 Python and C tests | Pass; roundtrip, block crossing, cache, bounds, CRC | C unit test uses an injected decoder |
| pi32v2 compile of six port/archive sources | Pass with pinned AC79 SDK headers/toolchain | No Doom core, zliblite link, or SDK link |
| pi32v2 compile of 79 original engine files | Pass with pinned SDK toolchain | Compile only |
| Offline SDK+Doom size probe, adapter rooted | Links; app sections 434,384 B without WAD; `.ram0_data` 68,048 B and `.ram0_bss` 342,080 B | Retains NES app and uses nonfunctional libc shims; not runnable Doom firmware |
| Experimental 160×100 size probe | Links; 433,296 B app sections before WAD, 343,088 B static RAM | Same probe limitations; selected archive makes 624,830 B before integration |
| Direct-E1M1 Doom-only size probe | Links without NES app; 363,888 B app sections before WAD; fixed 296 KiB zone and 4 KiB cache included in 477,656 B static RAM; 45,900 B linked heap span | Empty `app_main`, nonfunctional libc shims; not bootable Doom |
| Current menu Doom-only size probe | Links without NES app; 361,072 B app sections before WAD; 470,728 B static RAM; 52,812 B linked heap span; 478,662 B app plus FMD1 | Inert app and unsupported libc; size baseline only |
| Revised UBOOT `app.bin` input | Links Doom/USB tasks, XIP FMD1, shared LCD DMA strip and paced SPI2 scanner: 504,592 B app, 474,264 B static RAM, 49,292 B linked heap; reviewed startup reserve 4,672 B | Offline budget passes; physical runtime heap/stack still unmeasured |
| Startup and video audit | Correct SDK stack units; static bounded inflater; zone-backed lumpinfo; remove 48,000 B wipe peak; save 6,768 B drawing RAM; 64× fewer gameplay palette samples | Complete pixel reference comparison passes; this is not a whole-frame speedup claim |
| Audited host/decoder tests | Normal CTest 5/5, lowres x86 CTest 4/4, Python 21/21; 114 archive blocks decoded; 296 KiB x86 moving/menu/restart/exit pass | SDK headers with desktop zlib for decoder execution; target SDK compile/link pass |
| Current eight-row host output | 4,860 callbacks over 120 ticks; exact image hash match with four-row output | No hardware timing or FPS result |
| First FM-1 Doom flash | 121 sectors written; complete 1 MiB readback and decoded application/CRC match | User reported blank LCD after reset and cold boot; display/input acceptance failed |
| Doom CDC startup diagnosis | Second flash verifies 124 sectors/full readback; reset and CDC boot work, zero LCD error; fatal config name lacks its `%i` index | Generated printf normalization fixes the confirmed SDK incompatibility; serial UBOOT recovery passes |
| Corrected physical Doom boot | Six changed sectors/full readback match, reset succeeds, stage 4 and frames 133→167 with zero faults; user confirms visible gameplay | Controls still being checked; approximately 11 counter increments/s in short capture, not 30 FPS acceptance |
| Scanner freeze and graphics correction | 569,840 B app; 474,488 B static RAM; 49,068 B linker heap and 4,448 B reviewed reserve; 140 changed sectors/full readback match; reset/CDC succeeds | Whole-map memory/stack and 30 FPS remain open |
| Revised graphics and memory tests | Original pistol/punch, 4×4 world/enemies, Smooth/Detailed toggle; host600 firing/moving ticks and Detailed120 pass at296 KiB; CTest5/5 and4/4, Python30/30, all145 archive blocks decode | Host tests plus target compile/link; audio disabled |
| Sustained physical input after fix | User confirms stable movement/firing and both views; 60-second Detailed-mode capture frames629→1292 with zero engine/LCD/key errors, retry0 and advancing scanner/IRQ counters | Approximate11.05 counter increments/s; not distinct-frame30 FPS acceptance |

The first smoke used Freedoom 0.13.0 `freedoom1.wad` from the project's official
[release](https://github.com/freedoom/freedoom/releases/tag/v0.13.0), stored
locally outside Git. The screenshot stays in ignored `build/`. This is an IWAD
compatibility test of the original engine, not a performance measurement.
The E1M1 data test used a separately obtained Doom shareware 1.8 IWAD; none of
that input, the derived WAD, or the FMD1 archive is committed.
The host-only `FM1_DOOM_ZONE_KIB` setting overrides Doom's `-mb` minimum.
The 296 KiB result applies to the generated 32-bit low-memory builds, including
the current menu profile. That
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
direct-E1M1 menu FMD1 archive is 117,590 B with a 4 KiB cache. The current
candidate embeds it into read-only XIP flash and verifies the exact bytes in
the extracted app image. The candidate's app is 504,592 B, below the reviewed
584,956 B slot. Its fixed 296 KiB zone and 4 KiB cache are included in
474,264 B static RAM, along with a 7 KiB bounded inflater arena; the linker
heap span is 49,292 B. The reviewed task/queue/idle budget is 42,584 B, plus
800 B initialization allowance and 1,236 B USB allocations. Physical heap/stack use and flash behavior
still need measurement. See [TARGET_CANDIDATE.md](TARGET_CANDIDATE.md) and
[AUDIT.md](AUDIT.md).

## Required before calling this playable on FM-1

1. Verify boot and bounded XIP archive reads on the physical device, including
   corruption and error handling.
2. Exercise the fixed zone under the linked SDK Doom task and prove SRAM,
   stack, heap, and protected-flash budgets from live diagnostics and the ELF.
3. Qualify LCD, scanner and timer bindings; add responsive DAC audio and a
   recovery/control channel without ISR blocking.
4. Reproduce the host gameplay image through the LCD, then measure controls,
   frame rate, WAD latency, audio, battery behavior, and restart on the bench.
5. Preserve the known-good installed v32 image, verify a candidate and restore
   plan, and perform any physical write only as a deliberate bench step.

Until these gates pass, the generated private `build/target-candidate/app.bin`
is a UBOOT application input, not a hardware-qualified Doom release or a full
flash image. No game data or firmware binary is committed to the repository.
