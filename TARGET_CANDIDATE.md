# FM-1 Doom UBOOT `app.bin` input

## Current source revision (2026-10-08)

The source retains E1M1 and current 4×4 world/enemy assets. Gameplay always
uses the detailed 160×93 view; the 8×8 LCD presentation toggle is removed.
Source `783a03d` adds the shared NES filter/Wah effect with SELECT switching
between retained Synth and NES FX banks. Synth editing keeps its previous
VCO/VCF/VCA/reverb assignments; NES FX assigns cutoff, resonance and LFO
rate/depth, with exact NES presets and Bypass/LP/BP/HP modes. Bank selection
preserves the OPL/synth source; E4 remains its toggle. Original OPL, Synth bank
and bypassed NES FX are the defaults. This new source has **not been flashed**.
All 13 native contracts, including the exact original OPL reference, and shared
NES FX tests pass. The corrected target build/profile and budget gates pass;
independent code audit passed; physical acceptance remains pending. The current
artifact below is an offline candidate, separate from the installed Oct7 image.

The fixed 296 KiB allocation is partitioned into a 294 KiB Doom zone and a
2 KiB synth delay; the 4 KiB archive cache is unchanged. Source `626d2612` is installed and
boots with advancing game/audio telemetry. Its 139-sector update and complete
1 MiB readback match image SHA-256 `a4c3fc49...`. Physical editing, speaker output
and timing acceptance remain pending. Dated values below describe their named
checkpoints; see [deployment evidence](DEPLOYMENT.md#live-editor-deployment-2026-10-07).

### Offline NES FX artifact (2026-10-08; unflashed)

| Gate | Result |
| --- | --- |
| Built Doom / shared board revisions | `783a03d` / `2a577e8` |
| Plain UBOOT input | `build/target-candidate/app.bin`, 567,344 B |
| App SHA-256 | `4f3871be83e4332f1f592178484d0b51ad27d0e341b2878bfa02ebb9615a6488` |
| ELF SHA-256 | `3a6303ff0f0b5a46623a346bdb320823013a7a67b522cc4a1631704c1f7e87cf` |
| App slot / remaining | 584,956 B / 17,612 B |
| Static RAM: data + BSS | 26,928 + 452,280 = 479,208 B |
| Linker heap / reviewed startup reserve | 44,332 B / 4,320 B; required 4,096 B |
| USB stack / diagnostic chain / SDK margin | 2,560 / 1,480 / 1,024 B; 56 B remaining |
| Source closure / compilation | 442 hashes; 79 engine and 7 port units |
| Target gates | Archive/banks, XIP, DMA, descriptors, source, stack and RAM pass |
| Native contracts | 13/13 pass, including exact original OPL reference |
| Candidate/stack Python checks | 18/18 pass |
| Prepared shared filter | 226,084 samples match exact 64-bit reference; all 128 LFO rates, stale rejection and buffer/partition paths pass |
| IRQ arithmetic | Prepared-render target IR uses only 32-bit arithmetic; 64-bit LFO division stays in task preparation |
| Prepared encoded-image SHA-256 | `d6884aae995f6e170c9ceb981783376eca154aec3746cf78f617d536fde2dcef` |
| Offline update/restore validation | 139 sectors, directory last; boot/config/tail preserved |

The 170,636 B archive and all sound/music/OPL banks match the retained accepted
manifest. No device operation or new flash accompanies these build results.
Independent audit passed. Hardware editing response, speaker/streaming quality,
sustained DSP timing and 30 completed distinct frames/s remain open.

### Last installed live-editor artifact (2026-10-07)

| Gate | Result |
| --- | --- |
| Plain UBOOT input at this checkpoint | `build/target-candidate/app.bin`, 565,744 B |
| App SHA-256 | `70553874c1d8dd2e89e1448d27fbf89b1d5476b8573f3a9cb1e89fa46cfd3d35` |
| ELF SHA-256 | `56b7036a6d5f175e5dd519dbdf1243e20b96444170ecc45d14a5732a0f33f126` |
| Built source revision | `626d261208c6a1b9ae12783da524ea50c173b24d` |
| Verified full-image SHA-256 | `a4c3fc499f9a3bcf017d33bc48a7d2555e6eac04d52dde5863f3313b958c48a1` |
| App slot / remaining | 584,956 B / 19,212 B |
| Static RAM: data + BSS | 26,928 + 452,232 = 479,160 B |
| Linker heap / reviewed startup reserve | 44,396 B / 4,384 B |
| Required startup reserve | 4,096 B |
| USB stack / diagnostic chain / SDK margin | 2,560 / 1,476 / 1,024 B; 60 B remaining |
| Native contracts | 13/13 pass, including live controls and exact OPL reference |
| Engine memory smoke | 1,200-tick Win32 route at 294 KiB crosses first door and reaches enemies; 600 moving/firing ticks and menu open/restart/resume pass |

The manifest records source/dependency hashes, private bank/archive hashes,
emitted stack frames and unchanged reviewed app layout. Original game data,
banks and firmware remain private ignored build outputs. Passing these gates
establishes a valid UBOOT input. Separate deployment evidence establishes
readback and observed boot; live knob direction, DSP deadlines, runtime
high-water marks and 30 completed distinct frames/s remain open.

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
The historical `8a36a90` CPU/interrupt and door correction passed complete
readback and serial boot observation. The `60827f7` replacement restored IIS
startup and produced a real USB recording, but ADC control and audio timing
failed. A subsequent early-ADC image also failed physical knob control.
Configuring IIS and ADC before LCD/scanner setup, with polling during LCD
initialization, restored live and physically confirmed volume control.
Seven inlined OPL helpers improved production to about42,930 frames/s;
the latest candidate also skips silent operators while preserving reference PCM.
USB capture is before master gain. See [current bench evidence](PORT_STATUS.md).

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

### Incremental bench profile

The retained archive matching the last accepted installed manifest is
`build/fine-assets/audio-door-fixed.fmd`, 170,636 B, SHA-256
`197d7ccfc475893a6a5b581ea63c51003c524aaa4c38096de370329f2fd2fe7f`.
The complete accepted manifest is retained privately in
`build/live-edit-target.log`; sound/music/OPL banks are in `build/sound-bank`,
`build/music-original` and `build/opl-bank`. Check their content hashes before
an incremental build or a profile comparison. The existing local
`build/menu-ui-4k.fmd` was an older 117,590 B 8×8 archive. Reusing that filename
does not establish the same assets. The reproduction commands above regenerate
new assets and must be validated as their own closure.

The correct retained archive input for the current bench is:

```powershell
python tools/build_target_candidate.py build\fine-assets\audio-door-fixed.fmd --fm1-root F:\dev\fm1 --music-bank build\music-original\music_score.c
```

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

## Historical private-buffer and USB diagnostic candidate, 2026-10-07

The installed plain application is563,440 B, SHA-256
`ce8176d84946623efc296434973dbabc75c094edd3b7a8cb8ea29fbc39762ab3`.
ELF SHA-256 is
`a07105f20eef7596f407ecfb308475c64fba0e8af426ce6efbcab5b2121758df`.
Static RAM is479,000 B and linker heap is44,556 B. The reviewed startup
reserve is4,544 B, above the4,096 B required reserve; the USB task stack
remains2,560 B. Application slot headroom is21,516 B. Private assets are
unchanged.

Audio renders to a512 B private block, records the pre-master USB tap there,
applies the shared physical master envelope, then copies the completed block
to the SDK DMA half and issues `csync`. Low-gain and muted regressions fail
before this change and pass afterward. All12 native contracts pass, including
the exact complete-loop OPL reference. `update_noise` is now inlined along
with the seven earlier hot helpers, preserving arithmetic and state order.
The final copy still has the64-frame deadline; this change does not guarantee
that synthesis finishes within1.451 ms.

The optional F4 boot screen adds60 B of read-only USB clock/controller
snapshots. Hold F4 (eighth white piano key from the left) at power-on to show
stage/error/heartbeat and before/after registers for30 seconds; normal Doom
then starts. No clock or PHY register correction is applied by this screen.

The encoded image
`2a432d0f469a21169a5cbe638fb2c5f95e9be493845eadd2eda0cd92951a6a1b`
passed138-sector writing, directory last, and complete1 MiB readback.
Warm reset and COM4 serial observation passed at stage4/fault0, frames109→142,
with zero LCD/key errors. The user reports much improved, almost-perfect
speaker playback. USB still disappears after cold boot; the subsequent audio
capture could not open the absent COM4. Cold-boot USB, remaining speaker
glitches and physical output timing remain open.

## Previous IIS-first and monitor candidate, 2026-10-06

The previous plain application is 561,520 B, SHA-256
`6150df022dd98f525ea8fa59fa67020a4d0a9f62331be79a3d123ddd9cf15c7f`.
ELF SHA-256 is
`9062dd24c9896cb6f3b171518f8b940984e8e05cdee6182a0f3e26b08e0d6d17`.
Static RAM is 478,424 B and linker heap remains 45,132 B, with the
reviewed 5,120 B startup reserve and a 2,560 B USB task stack. The private
graphics/effect/music/GENMIDI assets are unchanged. All 12 native contracts
pass, including real-driver IIS/ADC ordering and exact OPL PCM comparisons.
The linked image has no calls or definitions for the seven inlined OPL helpers.

The prepared encoded image is
`71d2cc0d86965ac4ad146ba7493ec2eca2e43a1d82cf12f5236c441753bcf461`;
138 application/directory sectors change, directory last. Offline source,
CRC, protected-range and write/restore checks pass. This is separate from
physical knob control, callback-rate and sound-quality acceptance.

This build adds bounded15-second drum/melody/music-pause listening diagnostics.
Drums are original OPL GENMIDI instruments; the independent PCM bank contains
game effects. Percussion ownership is saved with each native chip output so
the first stem/crossfade sample is aligned. The complete original OPL reference
still matches37,784 register writes and4,233,600 PCM frames exactly.
The preceding8d7fc70d image passed readback/boot and a19.997-second actual USB
recording; its43,086.91 frames/s production and remaining speaker noise are
documented in [AUDIO.md](AUDIO.md). Normal cold-boot USB is unresolved.

The preceding 561,328 B application `909c0188...` was written and readback
verified as image `ac0af0b6...`, then booted at stage4 with zero faults/LCD/key
errors. A25.485-second OPL-mode sweep measured ADC0..1023, gain0..127/errors0,
and670.787 audio IRQ/s (42,930.35 stereo frames/s). The user confirms physical
music volume control; remaining noise is audible during music, particularly
at low master volume. This does not establish the latest candidate's deadline.

## Historical installed audio allocation revision

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
USB observation reached stage 4 with zero engine/LCD/key errors; frame capture
worked. IIS diagnostics showed `ready=0`, `error=-1`, `irqs=0`, `volume_valid=0`.
This revision's diagnostic chain is 1,436 B; its build gate reserves
a further 1,024 B for reviewed SDK paths and rejects lost handler boundaries,
unknown stack writes or nested
diagnostic calls. This scoped gate does not replace live stack/heap acceptance.
The combined bound leaves 612 B in the 3,072 B task allocation. Its complete
flash readback matches
`d2ed5405c1e1c24e8fc75bb77004d59e77a1d25035681953370a1e40a205ae76`.
One reset completed, but HELLO timed out while COM10 enumerated. The user now
confirms audible music, no knob effect and apparently unresponsive USB. Windows
currently sees no FM-1 port; fresh device diagnostics await reconnection.

## Historical CPU/volume candidate (subsequently installed in 8a36a90)

Doom and USB tasks use CPU0 `#C0` bindings. ALINK is owned by CPU1 at priority
3; CPU0's ALINK route is masked and teardown unregisters CPU1. The existing
CPU0 key timer polls PB6 ADC4 every two milliseconds under a separate volume
lock, publishing a volatile Q7 gain target to the CPU1 audio callback. ADC work
is absent from that callback. USB scalar audio status no longer takes the
mixer lock; its observations can straddle callbacks.

`DOOM AUDIO` adds `volume_target` and `volume_samples`. An integration contract
exercises the actual ADC driver and output envelope at full, half, zero and
restored gain plus conversion timeout. Python 72/72 and three native USB,
sound and volume-integration contracts pass. The integration test also proves
scalar diagnostics return while the mixer lock is held. All 79 engine and
seven port sources were freshly recompiled and the target link passes.

| Historical candidate item | Value |
| --- | --- |
| Plain app / slot headroom | 555,120 B / 29,836 B |
| App SHA-256 | `b0efff90e99d7f767c8e938f32d2906120f2fe07ff8604ec2b80c33a8315356e` |
| ELF SHA-256 | `cc757ada7c93c78570d77146784e49a692f8fe3d0f2b18d37e5f63de39ddffe5` |
| `.ram0_data` / `.ram0_bss` | 26,896 B / 451,000 B |
| Static RAM / linker heap | 477,896 B / 45,644 B |
| Reviewed task minimum / startup reserve | 37,464 B / 5,120 B before other allocations/padding |
| USB task / diagnostic chain / SDK margin | 3,072 B / 1,460 B / 1,024 B; 588 B remaining |
| Emitted status / trace / audio handler frames | 968 B / 944 B / 1,136 B |

These changes were subsequently installed in8a36a90 with the door correction.
They do not establish the cause of the observed knob fault. See the current
bench evidence above for later images, readback receipts and open USB/audio
acceptance; this table preserves the earlier build's measurements.

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
