# FM-1 Doom UBOOT `app.bin` input

The current low-memory E1M1 build has a real FM-1 SDK `app_main`. It creates a
separate Doom and USB tasks so SDK event dispatch and diagnostics continue. The task validates a local
FMD1 archive linked in XIP flash, initializes the recovered stock LCD sequence,
starts the SPI2 DMA2 scanner with a paced interrupt, and runs Doom. LCD rows use
the working NES row origin zero and the 240×240 RGB565 strip interface. The
converter shares the LCD driver's persistent DMA strip. Scanner bits have a
10 ms stability filter. The scanner uses the SDK's hardware-interpolated
500 µs clock; the coarse 10 ms clock could falsely hit its 10 ms watchdog.
Input failures release keys and retry with 50–1000 ms backoff while rendering
continues, resetting the streak after one second of healthy sweeps. Engine errors, including LCD
transfer failures, record a bounded RAM message and stop LCD/scanner/audio;
USB status and serial UBOOT remain available. Audio initialization failure is
nonfatal to rendering and is reported separately. The earlier `fc03a11` build
was audible; the `58b9ac4` USB repair reported an IIS initialization failure.
The latest allocation fix's boot and audio/knob acceptance remain pending.

The first-stage game contains E1M1 only. Its original image menu offers New
Game and graphic detail; completing E1M1 starts E1M1 again rather than entering
an absent intermission/E1M2. The new audio backend implements original sound
effects and the original E1M1 score through one shared IIS mixer; other music
tracks remain disabled. Save/load and persistent configuration are disabled.
`fm1_doom_frames`, `fm1_doom_last_frame_ms`,
`fm1_doom_max_frame_interval_ms`, `fm1_doom_slow_frames`, `fm1_doom_stage`, and
`fm1_doom_fault`, and `fm1_doom_error_message` are RAM diagnostics. They are
not a completed 30 FPS test.

## Reproduce without touching the device

Use a lawful local Doom shareware IWAD. The commands create only ignored local
artifacts; no game data is committed.

```powershell
git submodule update --init
python tools/make_lowres_engine.py
python tools/stage_wad.py C:\path\to\doom1.wad build\stage-menu-ui.wad --map E1M1 --silent --prune-graphics --prune-sprites --no-attract-art --no-ui --menu-ui --pixelate 4 --weapon-pixelate 1 --fist-pixelate 2 --compact-assets
python tools/pack_archive.py build\stage-menu-ui.wad build\menu-ui-4k.fmd --block-size 4096
python tools/make_sound_bank.py --wad C:\path\to\doom1.wad --menu --output-dir build\sound-bank
python tools/make_music_score.py C:\path\to\doom1.wad build\music-original
python tools/make_genmidi_bank.py C:\path\to\doom1.wad build\opl-bank
python tools/compile_target_engine.py --fm1-root F:\dev\fm1 --lowres
python tools/compile_target_port.py --fm1-root F:\dev\fm1 --lowres
python tools/build_target_candidate.py build\menu-ui-4k.fmd --fm1-root F:\dev\fm1 --music-bank build\music-original\music_score.c
```

The stage tool's `--silent` removes audio from the WAD. Sound and music are
generated separately from the lawful source IWAD into private XIP banks;
the shared IIS backend reads those banks without the FMD1 cache/inflater.

The builder requires the pinned SDK and a reviewed local FM-1 boot baseline.
It verifies every FMD1 block and CRC, checks that the exact archive bytes land
in the ELF's read-only `.text` and extracted app image, then enforces the stock
584,956 B V14/v32 application slot and 523,596 B RAM0 limit. It compiles the current
board root and LCD/key sources; unchanged baseline boot sources must still
match their recorded hashes. Outputs are
`build/target-candidate/fm1-doom-candidate.elf`,
`build/target-candidate/fm1-doom-candidate.app.bin`,
`build/target-candidate/app.bin`, and
`build/target-candidate/build-manifest.json`. **Use `app.bin` as the plain
application input for a WL82 UBOOT flow that accepts the SDK's `-app app.bin`
format.** It is the exact concatenation of `.text`, `.data`, `.dynamic_data`,
`.ram0_data`, and `.cache_ram_data`, with XIP entry `0x02000120`. It is not a
complete 1 MiB flash dump or an encrypted raw application region; the UBOOT
tool must handle its normal encoding, directory CRCs, and placement. Never
write these plain bytes directly at physical flash address `0x4120`.

The builder sets `flashable: true` for this UBOOT **input format** and
`hardware_boot_verified: false` at build time; subsequent physical deployment
is recorded below. The generated file includes derived game
data and is ignored by Git; keep it private. Check its size and SHA-256
against the manifest before loading it in your UBOOT program.

## Current audio allocation candidate: hardware verification pending

Revision `03492fc` produces a 555,024 B app, SHA-256
`fb230df6e1d5bf6f13232551d26638a1caea01eb012a2a3b2a67c91e6cf8b246`.
ELF SHA-256 is
`b7df5e2aa2220091f801daa8177b418e642757ed5fdd291fac365beca171aa0d`.
It leaves 29,932 B in the reviewed application slot. The losslessly compacted
archive is 170,505 B, SHA-256
`9fca855cbab806ea279510666b13186dbb9aae16c615783dd21df03211f97dcd`.
Private effect/score/GENMIDI banks are 12,613/10,975/563 B.
Static RAM is 477,912 B (`.ram0_data` 26,896 B, `.ram0_bss` 451,016 B).
The linker heap is 45,644 B. Reviewed tasks/queues/idle require 37,464 B,
plus 800 B initialization, 1,236 B USB and 1,024 B IIS DMA allowances, leaving
5,120 B before other allocations/padding. The USB task requests 768 SDK words,
or 3,072 B, returning 1 KiB to the runtime heap. This is a static budget.
An explicit `DOOM AUDIO` request reports aggregate remaining allocator space
as `heap_free`; it does not measure the largest block or minimum-ever free heap.

Unused FAT volume code is omitted; the three stock SDFILE configuration
drivers and their 360-byte registration remain. The builder checks that
closure. Doom reads its private XIP archive and does not mount a FAT volume.
Lossless graphics compaction keeps all 493 lump names/order, original decoded
patch/texture pixels and complete map data. It does not increase the 4 KiB
decode cache. See [audio controls and verification](AUDIO.md).

The prior 554,800 B revision `3318678` was installed with 136 changed sectors
and a matching complete readback. The user then reported silence; USB failed
after cold boot. Compiler inlining combined mutually exclusive reply buffers
in a 3,192 B task frame; the status call chain exceeded its 4,096 B stack.
The flashed `58b9ac4` repair emits separate handlers, a 136 B task frame and a
maximum 1,424 B diagnostic call chain. Full readback SHA-256 is
`71a78229cec1355d89f95d0a361cfcc751f07571397c0acd28f95b1480d61e2d`.
USB observation reaches stage 4 with zero engine/LCD/key errors; frame capture
works. IIS diagnostics show `ready=0`, `error=-1`, `irqs=0`, `volume_valid=0`.
The current candidate's diagnostic chain is 1,436 B; its build gate reserves
a further 1,024 B for reviewed SDK paths and rejects lost handler boundaries,
unknown stack writes or nested
diagnostic calls. This scoped gate does not replace live stack/heap acceptance.
The combined bound leaves 612 B in the 3,072 B task allocation. The current
candidate's complete flash readback matches
`d2ed5405c1e1c24e8fc75bb77004d59e77a1d25035681953370a1e40a205ae76`.
One reset completed, but HELLO times out while COM10 enumerates. Cold-boot
observation, physical audio/knob acceptance, IIS deadline and sustained controls
remain to be checked.

## Historical installed audio application

The final local application is 583,216 B, leaving 1,740 B in the reviewed
584,956 B slot. Its SHA-256 is
`715f58b29fd2f0280ead27adf975aa5908b6832b5b6ebf92634ca7426a9927a5`.
The selected shareware E1M1 archive is 177,331 B, SHA-256
`551640b7f5f8c747c19a15353cd1657aaa2eaa49ea22c53f81c40912a227623b`.
It retains original pistol/menu detail, 2×2 fist patches and 4×4 world/enemies.
The private sound/music payloads are 6,064 B and 10,722 B and are included in
the app image.

| Memory item | Bytes |
| --- | ---: |
| `.ram0_data` | 26,800 |
| `.ram0_bss` | 448,376 |
| Total static RAM | 475,176 |
| Linker heap before runtime | 48,364 |
| Reviewed tasks/queues/two idle tasks | 38,488 |
| Initialization allowance | 800 |
| USB heap requests | 1,236 |
| IIS DMA allocation | 1,024 |
| Reserve after reviewed startup | 6,816 |
| Required linker heap, including 4,096 B minimum reserve | 45,644 |

Doom requests 2048 SDK words/8 KiB; USB requests 1024 words/4 KiB and reports
unused stack words through `DOOM AUDIO`. The installed graphics/input milestone
was 569,840 B with a 181,995 B archive, 474,488 B static RAM and 49,068 B heap;
that older image used an 8 KiB USB stack and had audio disabled.
The inflater uses a 7 KiB static arena and explicit allocation callbacks;
the lump directory uses the fixed Doom zone. Both are included in static RAM.
The builder requires at least 4096 B of reserve after the reviewed startup
budget. The archive is included in those app bytes,
not added afterward. The ELF/app bytes and archive hashes are in the local
manifest. All 411 closure hashes match; 43 Python tests and four focused
audio/protocol/formatter CTests pass. These offline checks establish layout
and static placement; the subsequent flash/boot evidence below is separate
from runtime memory and sound-quality acceptance.

## Existing protected writer

The SDK downloader accepts `-app app.bin`. The local protected writer already
accepts compiled builds through its preparation path, and writes only changed
4 KiB sectors. Its internal request contains a complete encoded image for
comparison and readback; it does not rewrite all flash sectors.

The Doom adapter prepares that existing request from `app.bin`, its manifest,
and the protected session's verified current readback. It checks source/header
hashes, app/ELF hashes, directory and payload CRCs, preserved boot/config/tail
bytes, and simulated write/restore. It performs no device I/O and stores no key:

```powershell
python tools/prepare_flash_request.py --fm1-root F:\dev\fm1 --build build\target-candidate --session "C:\Program Files\FM1FlashSession-<verified-current-session>"
```

Use the resulting private request with the existing `flash-session-client.ps1`
`plan`, `enter_uboot`, and `flash` operations. After one reset, `observe` verifies
the Doom CDC identity, running engine and advancing LCD transfer counters.
Physical LCD/input observation is also required. `DOOM STATUS` reports stage,
fault, frames, LCD/key errors, clocks and the bounded engine error message.
`DOOM STOP` shuts down peripherals cooperatively; `UBOOT` and the existing
confirmation handshake enter download mode only after Doom has stopped.
`DOOM AUDIO` reports sound/music readiness, errors and progress, USB unused
stack words and maximum observed IRQ duration. Duration is quantized to
500 µs, while each 64-frame audio half has about a 1.451 ms deadline; IRQ/frame
counters do not establish that no DMA deadlines were missed.

## Deployment and remaining gates

The first 493,104 B application was flashed on 2026-10-05 and its complete
1 MiB readback matched the prepared image. Decoding that readback reproduced
the exact app bytes at offset `0x4120` and valid payload/directory CRCs.
The user observed a blank LCD after reset and a normal power cycle.
That attempt did not establish working Doom boot. The revised 504,592 B build
adds the diagnostic channel and the NES startup corrections described above.
Its flash/readback/reset and CDC boot passed. The engine reported
`Unknown configuration variable: 'joystick_physical_button'` before frame zero:
the SDK formatter omits `%i`. Generated printf formats now use equivalent `%d`,
while scanf `%i` remains unchanged. Serial UBOOT recovery worked after the fault.
The final correction passed six-sector/full-readback verification and reset.
At that corrected boot, CDC reported a running engine and advancing frames
with zero faults; the user confirmed visible gameplay. The later 569,840 B
milestone established stable physical controls and both graphics views.
See [DEPLOYMENT.md](DEPLOYMENT.md).

The 583,216 B audio application was then written and fully readback-verified:
143 changed sectors, with directory sector `0x4000` last. Complete readback
SHA-256 is `ce9544e7730372690c9912a83d17ae73d641406c9153d26ada64ce3a7eea6ca9`.
Reset and COM6 `DOOM-FM1/1` observation passed at stage 4, frames 149→182,
with zero engine/LCD/key errors. The user hears both music and sound effects;
balance/timbre remains under review.

In a separate 105-second Detailed-mode capture, frames advanced 1084→2220
over 104.078 seconds (about 10.91489 counter increments/s). Audio IRQs advanced
69160→140162, output frames 4426240→8970368 and music loops 1→2; audio was
ready/playing with zero audio/music errors and no scanner failures/retries.
USB minimum unused stack was 378 words/1512 B. Maximum reported audio IRQ
duration was 500 µs, quantized to 500 µs, so that measurement was below
1000 µs; it does not establish underrun-free audio. This capture adds no new
physical movement/firing confirmation.

- The 32-bit Windows host completes 300 moving ticks and 80-tick menu/restart/
  exit scripts with a 296 KiB Doom zone, including the zone-backed lump
  directory. Transition wipes are disabled. A virtual-IWAD host run starts from an FMD1 archive even
  when no `doom1.wad` file exists on the filesystem.
- Host tests check LCD commands, row-zero placement, eight-row transfer, failure
  propagation, key debounce, and every optimized output pixel against the
  original mapper. SDK-header inflater tests use a real desktop zlib DLL and
  decoded all 114 blocks of the earlier menu archive; target compilation checks
  the SDK binding. The builder validates every block of the final archive.
- Physical boot, visible gameplay, sustained controls, both graphics views and
  serial UBOOT recovery are established for the historical 569,840 B image.
  The historical 583,216 B image adds verified flash/readback/boot, audible
  music/effects, sustained audio/scanner progress and USB stack telemetry.
  Sound quality, current audio shutdown, runtime heap/other stack headroom,
  full-map traversal and 30 completed distinct gameplay frames/s remain open.
- Preserve the known-good installed v32 image and a reviewed restore path before
  any physical write. No device operation is performed by the builder.
