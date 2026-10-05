# FM-1 Doom audit, 2026-10-05–06

The audit inspected the current sources, pinned SDK headers and library
bitcode, and the linked PI32V2 instructions. These corrections are implemented:

| Finding | Correction | Evidence |
| --- | --- | --- |
| Doom requested 8192 words, or 32 KiB, exceeding startup heap | Doom uses 2048 words/8 KiB; USB diagnostics use 1024 words/4 KiB; enforce the complete startup and IIS DMA budget | Pinned SDK stack units and task/control-block allocations inspected; original pre-audio six-task requirement was 48,716 B |
| SDK `uncompress` passes null callbacks that its inflater rejects | Explicit callbacks, 7 KiB static arena, one complete `Z_FINISH` call | SDK state allocation is 7,120 B; desktop binding tests decode 114 archive blocks and reject corruption/truncation |
| 493-entry lump directory consumes 13,804 B SDK heap | Allocate zeroed directory inside the fixed Doom zone | 32-bit 296 KiB host passes moving/menu/restart/exit scripts |
| Desktop error handler fails to report or shut down target I/O | Bounded RAM message, fault/stage update, peripheral stop and watchdog loop | Linked `I_Error` instructions call formatting, stop both peripherals and loop safely |
| Large drawing lookups retain 1120×832 capacities | Size lookup arrays for the fixed 160×100 screen | 6,768 B static RAM saved |
| Transition wipe peaks at three 16,000 B screen allocations | Remove captures, transpose and animation waits | 48,000 B transition peak removed |
| Eight identical LCD rows repeat palette sampling | Expand one sampled row and copy seven rows | Gameplay lookups fall from 53,760 to 840 per frame; all output pixels match the reference |
| CRC loops eight times per byte | Use two nibble table steps | Existing CRC tests and complete archive reads pass |
| Disabled console formats output that is discarded | Discard routine console output directly; retain fatal RAM message | SDK `printf` has no output transport |
| Build freshness ignores included headers | Check recursive headers/configuration and freeze their hashes | Engine/port objects checked; final audio closure includes 411 source/config/header files |
| Converter and LCD driver each hold a 3,840 B strip | Share the driver's persistent synchronous DMA strip and skip alias copies | Full pixel comparison and external-buffer contract pass |
| Switch/animation definitions occupy mutable RAM | Make both generated definition tables constant in XIP | 1,464 B tables; 1,472 B recovered in the actual link including alignment |
| Synchronous key-scanner startup can hide the first frame on failure | Reuse NES DMA2/paced IRQ scanning with neutral input and bounded retries | Working NES documented `KEY_START rc=-3 frame=0`; scanner BUSY is handled separately |
| LCD row origin differs from working NES | Use rows 0..239 | Strip tests cover first and last row bounds |
| Fatal errors have no transport | Keep CDC live with bounded status/error reporting and guarded serial UBOOT | Protocol tests and target CDC/UBOOT link-symbol checks pass |
| SDK formatter omits signed `%i` values | Normalize generated printf conversions to equivalent `%d`; retain scanf `%i` | Actual CDC reports unknown `joystick_physical_button`; SDK formatter IR lacks case 105; five normalization tests pass |
| Scanner's coarse 10 ms clock equals its 10 ms timeout | Use SDK hardware-interpolated half-millisecond clock | Actual frame counters continue while key error is -3; compiled SDK IR confirms clock granularity |
| Three lifetime input retries permanently disable controls | Continue bounded 50–1000 ms recovery, reset streak after one second healthy | Input remains neutral while recovery is pending; TRACE exposes progress and failures |
| Composite textures need fragmented 16/32 KiB zone blocks | Compose one column into a 135-byte static buffer | Stock C comparisons cover 2,711 columns per profile; formerly failing fire/movement stress passes 600 ticks |
| Weapon art uses 320-wide coordinates but new scale divides by 160 | Keep original 320-wide reference in weapon scale/inverse | Reciprocal scale/projection tests; visible firing pistol in host output |
| One coarse presentation cannot show fine sprites | Slot 24 toggles Smooth/Detailed; original pistol/punch plus finer world/enemy assets | Both modes match full pixel references; toggle holds/releases/simultaneous events pass; target struct size unchanged |
| Original sound effects and music are disabled | Mix two streamed original IMA ADPCM voices and synthesize the original E1M1 score with eight integer voices | Host tests cover ADPCM bounds, stereo mixing, original score timing, loop/stop, malformed data and actual engine-module controls; user confirms audible music/effects, with balance/timbre still under review |
| Music cannot use the archive inflater inside an audio IRQ | Keep a private immutable score bank in XIP with a bounded decoder and static synth state | No allocation, shared FMD1 cache or inflater access in the audio callback; original 140 Hz score timing retained |
| IIS DMA allocation is absent from the startup model | Include the pinned driver's 1024 B ping-pong buffer in the heap requirement | Channel 3, 128 points and 24-bit stereo produce 64 frames/512 B per callback |
| Audio shutdown could race the SDK callback | Use an attributed ALINK ISR and one IRQ-safe sound/music lock; quiesce the IRQ before IIS close | Host lifecycle/control tests and linked call-chain review; physical shutdown verification remains pending |
| SDK format/string archive members retain software floating point | Use bounded integer formatting, base-aware integer parsing and local allocation/copy `strdup` | The first audio link was 595,216 B; an intermediate correction was 593,488 B. Both exceeded the 584,956 B slot and were not flash candidates |

The trimmed main menu contains only New Game and Options. Its original image
patches and finer menu scaling remain. Gameplay toggles between exact 8×8 LCD
blocks and all 160×93 gameplay samples, with detailed HUD in both modes.

## Current OPL/synth/volume revision: offline verification

- The WHX music conversion inverted some pitch bends. The target now uses the
  native original 5,826-event MUS score. All 37,784 original DOS OPL register
  writes and 4,233,600 PCM output frames match an independent unchanged
  driver/core reference through the complete loop and volume changes.
- Original GENMIDI patches use a 563 B sparse bank. Effects use lossless PCM8
  Rice coding with exact duration, original menu open/close samples and
  identical noway/oof sharing. The 12,613 B effect bank is 6.02 dB quieter in
  the final mixer. It contains a subset of original effects.
- E4 selects DOS OPL or a default VCO/VCA/VCF synth treatment. The latter
  keeps OPL percussion at half amplitude. Tests cover eight-voice filter
  stability, release-to-silence, mode transitions and mute.
- PB6 ADC4 controls master volume, using bounded existing driver work in
  the audio callback. Startup/conversion errors mute safely; diagnostics
  report raw value, gain, validity and errors. No new task/heap is added.
- Unused FAT closure removal saves 41,504 B in the original size probe,
  retaining the three SDFILE configuration drivers. Final link checks their
  exact 360-byte registration and rejects unintended FAT drivers.
- Lossless graphics compaction reduces the archive from 177,331 to 170,505 B.
  All 493 names/order, 424 decoded patches and 70 composite textures remain;
  all maps/flats/marker data match. No cache or graphics fidelity reduction.
- USB GAME/frame capture uses existing pixels/palette and 128 B persistent
  diagnostics state. It releases on disconnect/error/STOP or 15 seconds;
  audio and scanner continue during a brief between-tick game pause.
- Repaired app: 554,768 B, SHA-256
  `64194b06e5b372e0b671b690e04b7a90aad56a89fc59380ad055812f74a6e373`.
  Static RAM: 477,912 B; linker heap: 45,644 B; reviewed reserve: 4,096 B.
  This is a static floor, not a runtime free-heap measurement.
- Python 59/59 and native contracts pass; all 79 engine/7 port sources compile
  for the target and SDK link passes. Final 16-second OPL/synth mixes have
  zero clipping/music errors. Mixed peaks are -12.79/-16.52 dBFS.
- Host navigation with installed and compacted maps reaches the corridor,
  opens the first door with one F5 edge, and reaches active enemies. The
  user's small-area report has no reproduced collision defect; actual device
  capture and whole-level traversal remain pending.

The prior 554,800 B revision `3318678` was flashed, with all 136 changed sectors
and the complete image readback verified. Cold boot leaves USB requests failing
and the user reports no audio. The emitted USB task frame is 3,192 B; its
periodic status chain reaches 4,320 B on a 4,096 B task stack. The repair keeps
reply buffers in separate frames: task 136 B, diagnostic maximum 1,424 B.
The builder gates actual emitted frames with 1,024 B additional SDK margin;
interrupt SSP is separate (audio closure 384 B of 4,096 B).
Repair installation and the cause of the audio silence remain unverified.
Physical volume, audio IRQ deadline, screen capture, sustained controls and
30 FPS remain device checks.

## Historical installed graphics/input milestone

- App: 569,840 B, SHA-256 `0463c496f4bf44a12af49ef0ab653808bffd8bc85ceda6ea7d73f036f4f4ef76`.
- Static RAM: 474,488 B; linker heap: 49,068 B.
- Reviewed tasks/queues/idle: 42,584 B; initialization allowance: 800 B;
  measured USB heap requests: 1,236 B; reserve: 4,448 B before other allocations and padding.
- Normal CTest: 5/5; low-resolution x86 CTest: 4/4; Python tests: 30/30.
- Target compilation: 79 engine files and 7 port sources; SDK link passes.
- That milestone's update/restore simulation passes; it changed 140 sectors, directory last.

This 569,840 B image is the established graphics/input milestone, with physical
boot, stable controls and serial recovery confirmed. Its audio was disabled.
The installed sound/music milestone is described below. Full-map traversal,
runtime heap and broader stack headroom, audio shutdown, sound quality and
30 FPS acceptance remain separate device checks.

## Installed audio milestone, 2026-10-06

The final local manifest records a 583,216 B application, below the 584,956 B
slot by 1,740 B. Its SHA-256 is
`715f58b29fd2f0280ead27adf975aa5908b6832b5b6ebf92634ca7426a9927a5`.
The 177,331 B graphics archive keeps the pistol at original detail, reduces
the four fist patches to 2×2, and keeps world/enemies at 4×4 with original menu
patches unchanged. Its SHA-256 is
`551640b7f5f8c747c19a15353cd1657aaa2eaa49ea22c53f81c40912a227623b`.
Sound and music XIP payloads are 6,064 B and 10,722 B, included in the app.

| RAM/heap item | Bytes |
| --- | ---: |
| `.ram0_data` | 26,800 |
| `.ram0_bss` | 448,376 |
| Total static RAM | 475,176 |
| Linker heap before runtime | 48,364 |
| Reviewed tasks/queues/two idle tasks | 38,488 |
| Initialization allowance | 800 |
| Measured USB heap requests | 1,236 |
| IIS ping-pong DMA allocation | 1,024 |
| Reserve after reviewed startup | 6,816 |
| Minimum required reserve | 4,096 |
| Required linker heap including minimum reserve | 45,644 |

All 411 source/config/header closure hashes match. The final Python suite
passes 43 tests; the four focused audio/protocol/formatter CTests pass.
The 583,216 B image is now flashed and running. Operation
`9410445302504833b3bd7b79742075ff` wrote and readback-verified 143 changed
sectors (`0x5000`–`0x92000`, then directory sector `0x4000` last). The complete
1 MiB readback SHA-256 is
`ce9544e7730372690c9912a83d17ae73d641406c9153d26ada64ce3a7eea6ca9`.
Reset `c40cb8c08c274f3a833f0207f8c6ad7b` and observe
`2d59691e4abf42988ffb2c8fa9e50d64` found `DOOM-FM1/1` on COM6, stage 4,
frames 149→182 and zero engine/LCD/key errors.

A separate 105-second Detailed-mode capture recorded 209 STATUS samples:
frames 1084→2220 over 104.078 seconds, about 10.91489 counter increments/s.
Its 104 AUDIO samples showed IRQs 69160→140162, audio frames
4426240→8970368, music loops 1→2, readiness 1, music playing 1 and zero
audio/music errors. Scanner counters advanced with zero failures/retries.
USB minimum unused stack was 378 words/1512 B. The maximum measured audio
IRQ duration was 500 µs with 500 µs quantization, placing that measurement
below 1000 µs; this does not measure underruns.

The user confirms hearing music and sound effects, but requested improved
balance/timbre and described the synth as sine-like. Sound-quality acceptance
is still open. The capture does not establish new physical movement/firing
acceptance; those controls were confirmed at the historical graphics/input
milestone. Full-map traversal, current audio shutdown, runtime heap and
30 distinct gameplay frames/s remain open. See [DEPLOYMENT.md](DEPLOYMENT.md)
and [AUDIO.md](AUDIO.md).

## Audio implementation and timing evidence

The new backend shares one 44.1 kHz IIS output between two sound-effect voices
and an eight-voice integer music synth. The score preserves original E1M1
events and 140 Hz timing; instruments are simplified rather than OPL emulation.
The source reaches ten simultaneous distinct notes, so an eight-voice mixer
can steal a voice. Stereo pan, volume and pitch events are handled. Private
game-data banks remain under ignored `build/` paths.

`DOOM AUDIO` exposes initialization/error status, IIS IRQ and frame counters,
sound starts, music progress/loops/voice steals and USB stack high-water words.
The IRQ duration field is quantized to 500 µs: a reported zero is not zero
execution time, and the actual duration can be less than 500 µs higher than
the reported value. The 64-frame DMA half has about a 1.451 ms deadline.
Advancing counters alone do not prove that every DMA deadline was met.

The linked pre-formatter audio ELF used a 1464 B USB task frame and a 932 B
status frame; integer formatting added about 220 B at the deepest normal call
chain. The ALINK callback chain used about 196 B, and the SDK has separate
4 KiB supervisor IRQ stacks. These historical call-chain estimates are
separate from the final installed image's measured 1512 B minimum unused USB
stack. Other task/IRQ stack watermarks and runtime heap remain unmeasured.

## App input and writer

The existing SDK downloader accepts `-app app.bin`; the protected local writer
uses prepared build requests. Doom now has an adapter for that preparation
step, so the service protocol needs no modification. The adapter uses the
protected session's verified current readback, preserves boot/configuration/reserved data and
updates application encoding and CRCs internally. Its full image buffer is
private; the writer programs only changed sectors.

The first Doom image's full readback and decoded app matched, but the user
reported a blank LCD after reset and cold boot. Offset `0x4120`, directory and
payload CRCs are verified. The second image passed 124-sector/full-readback
verification and reset; CDC reported stage 255, fault -30 and zero frames:
`Unknown configuration variable: 'joystick_physical_button'`. The target SDK
formatter drops the `%i` index; printf normalization corrects all three
configuration-name binders and integer diagnostics. CDC remained usable after
the fatal error, and serial STOP/UBOOT returned the device to download mode.
The stock/working NES initializer drives PA2 low; that behavior is retained.
At that point physical display/input acceptance remained open.

The final printf correction passed six-sector and full-readback verification.
After one reset, CDC reports stage 4, advancing frames and zero engine/LCD/key
faults. The user confirmed visible gameplay; physical controls and the
30 FPS target were still open at that point. Subsequent input
failure was diagnosed and corrected as described above. The revised graphics
and scanner image passed 140-sector/full-readback verification, reset and CDC
observation with zero errors.
The user then confirmed stable movement/firing and both views. A 60-second
Detailed-mode capture advanced frames 629→1292 with zero faults/retries;
approximately 11.05 counter increments/s does not satisfy distinct-frame
30 FPS acceptance. See [DEPLOYMENT.md](DEPLOYMENT.md).
