# Port status and acceptance gates

## Current source scope (2026-10-07)

E1M1 remains the only level. Gameplay always uses the detailed 160×93 view,
scaled to 240×224 above the 16-row HUD; the 8×8 presentation path and D♯4
toggle are removed. Existing 4×4 world/enemy assets, original pistol pixels
and 2×2 fist assets remain. Higher-resolution work was cancelled.

Live sound editing now uses the proven NES digital encoder decoder. Algorithm
selects four synth configurations; Knobs 1–4 control VCO detune, VCF cutoff,
VCA contour and reverb mix. Presets selects Original, Warm, Acid or Room.
Original OPL is the boot default; E4 still toggles the synth. The PB6/ADC4
master volume remains separate. A read-only `DOOM EDIT` query exposes controls.
Reverb has a dedicated 2 KiB buffer partitioned from the previous fixed zone,
leaving 294 KiB for Doom. New controls have not yet been tested on hardware.

The dated measurements below describe prior builds; they do not establish
performance or acceptance of the new live editor.

Offline editor checks pass: 13 native contracts, exact original OPL reference,
target engine/port compilation, flash/RAM/source closure and emitted USB stack
gates. The app is 565,744 B with 19,212 B flash margin; reviewed startup reserve
is 4,384 B. A Win32 1,200-tick route at 294 KiB reaches sector 52 beyond the
first door, with active enemy damage and 89,484 B purgeable/free zone memory.
This run uses the byte-equivalent staged WAD; it does not measure device FPS
or establish whole-level traversal. Details: [candidate](TARGET_CANDIDATE.md).

## Latest bench evidence (2026-10-07)

**Cable finding:** the programmer cable was connected. The user switched to
a normal USB data cable and the connection began working. Windows exposes
healthy COM5 and the UAC recording endpoint; the
existing Doom application passes HELLO and live observation (stage4/fault0,
frames1375->1409, LCD/key errors0). No further flash was performed. A firmware
cold-startup defect has not been established by the earlier cable-dependent
failure. Details: [USB_COLD_BOOT_AUDIT.md](USB_COLD_BOOT_AUDIT.md).

USB startup candidate `3cf7f681...` follows NES peripheral-first task creation
and adds an explicit IIS/ADC completion barrier before USB attachment. Build,
RAM/stack gates, 12 native tests and the actual USB task regression pass.
The application was flashed (138 sectors) and full image readback matched
`af92a30b658ee2f878c9b410ee53bf50391af9e3f3d86470500ada01de12f1f9`.
Warm reset exposes COM4 and UAC; frames123->157, stage4/fault0, LCD/key errors0.
Cold boot FAILED again: the user reports absent USB and Windows confirms no
FM-1 interfaces. Task ordering is insufficient. No further flash is authorized
until the cause is identified. The user confirms PCM and volume are OK;
that investigation is superseded by the cable finding above.

The previous `ce8176d8...` DMA-output image was installed, full-readback verified and
observed after warm reset. The user reports speaker improvement, but cold-boot
USB still disappears. A subsequent capture could not open because Windows
exposed no FM-1 device; exact disappearance timing is unconfirmed. Host music
PCM has no clipping and matches the original driver/core, without proving
physical noise or DMA timing acceptance.

The UAC/headroom revision `60827f7` was flashed and complete-readback
verified: application 557,968 B, SHA-256
`71cea5cef5aa3f8beeb175e99a0d618a6291438d37422ea340daed79f7fc9d52`;
full 1 MiB image SHA-256
`ae08519409aa26f81af53ab83ed03ddf7813ccf6dc4096af6cf879f204a2b020`.
After UBOOT reset, stage 4 and IIS `ready=1/error=0` were observed, with advancing
audio callbacks, zero engine/LCD/key errors and aggregate `heap_free=4244`.
The 1,024 B headroom recovery (USB stack 3,072→2,560 B, CDC ring 1,024→512 B)
restored audio initialization in this image. A reply remains capped at 511 B.

UAC capture produced a 19.779-second recording at 44,100 Hz, stereo PCM16. Peak was
-13.70 dBFS and RMS -31.08 dBFS, with no fullscale endpoints. Substantial streaming
underflows occurred in that image; the measured producer rate was about 33,454 stereo frames/s
against 44,100 required. Capture taps the mix before physical master gain, so
these samples do not establish working speaker volume or DMA deadline
acceptance. OPL is the boot mode. Audio performance work has resumed following
the user's latest quality report. The later startup revision restores ADC/gain
response, although the precise integration cause remains unresolved.

During a confirmed 20-second physical knob sweep on `60827f7`, all 26 `DOOM VOLUME`
snapshots returned raw0; samples advanced 30248→35552 with valid1/errors0.
ADC_CON remained 0x0000f49e and the observed PB/analog gates were unchanged.
The older `8a36a90` sweep had instead been stuck at raw260..261. Neither result
establishes the cause of Doom's failed physical knob input.

The exact older CDC NES reference was then installed on the same FM-1, with
application SHA-256 starting `d01690af` and complete readback SHA-256 starting
`1d9a5001`. Its live ADC readings traversed 0..1023 with zero errors and roughly
500 samples/s. The user confirmed the physical knob works. This A/B result
establishes that the knob and unchanged shared ADC driver work in that firmware;
it does not identify the failing Doom integration step.

The newer [public NES UAC profile](https://github.com/Keitark/fm1-nes/blob/9fa8d2352a9964241e3edbf16f9d2d22e8552623/firmware/usb-audio/profile.c)
also supports 48,000 Hz, stereo PCM16 playback and capture. Its retained 2026-10-03
deployment verified both Windows audio endpoints, 51 changed sectors and full
image SHA-256
`462751778c8b00ab6117f5752c59961eeb10046924fd72d64f06f27d6b5385d0`.
That newer UAC path starts IIS before ADC and polls ADC from CPU0 DAC IRQ cadence.
It is separate from the legacy CDC NES image used for the knob A/B test.

### Early-ADC image: installed, knob still fails

The early-ADC image starts volume before registering the scanner timer,
matching legacy CDC NES/MDX ordering. Sound initialization preserves already
primed raw/accepted/target/sample state and registers. ADC startup or conversion
errors cannot trigger an implicit retry; explicit shutdown permits a new
lifecycle. The shared ADC source, channel/pin and register settings are unchanged.
This removes an integration difference; it did not resolve the user's reported
knob failure and does not establish startup ordering as the cause.

Application 558,000 B, SHA-256
`998677b7775b4ebb9a7a8fe55d8884b498fc4f4d0ab655a75923609ea8f6e948`;
packaged full-image SHA-256
`f328adbc1c08cafd78d41547f5f6947878266da407ae49a7c985f3d1062d81a7`.
Static RAM remains 478,408 B, linker heap 45,132 B and reviewed startup reserve 5,120 B.
Lifecycle/mute flags occupy bits 30/31 of the existing volume control word;
the change adds no static RAM. All 12 native contracts pass, including real-driver
early priming/state preservation and ADC-error lifecycle regressions.
Installation completed with 137 changed sectors and full readback matching
the packaged image hash above.
Reset and observation passed on COM4: stage4/fault0, zero LCD/key errors and
frames129→163. At that checkpoint the user reported the knob still did not work. A 3.84-second
initial capture showed raw221..222, accepted222, target=gain27, errors0 and
samples11030→11938; IIS reported ready1 and aggregate heap_free4244.
The changed constant reading, compared with `60827f7` raw0, is not proof of
physical control. That image left ADC diagnosis open.

### Volume-control image: IIS before ADC, input and gain respond

Application 561,328 B, SHA-256
`909c0188d2f0f68936c2a99812d76c756f29f0ef553773ff85332d6c79ac59dc`;
packaged full-image SHA-256
`ac0af0b6aa4238bd8ec2a6689a2228adeff800f622af09a0f924f1e9330475a7`.
The hardware startup order is IIS open/callback/rate → ADC → ALINK, before
LCD/scanner initialization, matching newer UAC NES initialization. Doom retains
CPU0 timer polling and CPU1 audio rendering. Its existing 1 ms timer now starts
before LCD setup, polls ADC every 2 ms during that setup, and is reused when
scanner setup finishes. Scanner work is guarded until it is enabled.

Seven hot OPL helpers are forced inline; the target symbol audit finds no
remaining calls to those helpers and exact reference output is preserved.
All 12 native contracts pass, including default IIS/rate-before-ADC ordering,
no ADC access on IIS failure, and preservation of explicitly primed ADC state.
Static RAM remains 26,896 B data plus 451,512 B BSS, with 45,132 B linker heap.
Installation changed 138 sectors and complete readback matched the packaged
image hash above. Reset/observation passed on COM4: stage4/fault0, zero LCD/key
errors and frames19→53.

The confirmed approximately 25.5-second knob sweep in
`build/adc-iis-first-knob-live.jsonl` contains 37 VOLUME snapshots: raw and accepted
ADC0..1023, target/gain0..127, errors0 and samples28174→37836. IIS remained
ready1/error0 and aggregate heap_free4244. The grouped IIS/ADC-before-LCD plus
early polling change restores ADC-to-gain response. The user confirms OPL
music loudness follows the knob correctly. This result does not isolate a
single register or clock interaction as the cause. Noise is still reported
during music, especially at low master volume; its source has not been isolated
to synthesis, PCM effects or output timing.

All 37 AUDIO snapshots remained in default OPL mode0 over 25.485 seconds.
IRQs50275→67370 and outputframes3217600→4311680 give 670.79 IRQ/s and
42,930.35 stereo frames/s, improved from the earlier 33,454 but below the
689.06 IRQ/s and 44,100 frames/s required. The cumulative max_irq_us3500 includes
earlier synth mode1 activity; it is not an OPL-only maximum. That image's new recording attempt could
not start when Windows exposed neither serial nor UAC. Windows subsequently
enumerated a healthy UBOOT device; missing application interfaces do not
establish a runtime USB hang or its cause. The earlier `60827f7` UAC recording
remains verified.

The shared ADC MMIO sequence matches the working NES machine code; the previous
leaf-function versus inline presentation did not expose a register difference.
The proposed VBG/internal-reference probe was not implemented because SDK
P33 locking and SPI-transfer waits are unbounded and no safe exported try-lock,
cancellation or restoration path was verified. No P33 gates or reference
settings are changed. The startup/polling comparison now has a responsive
physical ADC sweep; the precise failing integration step remains unresolved.

### Earlier readback-verified image: exact silent-operator optimization

Application 561,040 B, SHA-256
`8d7fc70db0535a1e5a116d9f527f686b5bfe6d6e0e2b60ba23f755c9d5e244c7`;
ELF SHA-256
`0d75e672f94dd59e2d80e1ff1787d0c9bbb44baa0e6131306c2f59d933aef803`;
packaged full-image SHA-256
`50d6ee1037e4761667a9f4c881cbe1fc7f79234fbcc4b4aeab836b813c4be101`.
The image preserves the verified IIS-before-ADC/early-polling startup and
adds an exact OPL path for silent operators (eg_out>=EG_MAX). It skips waveform,
AM and feedback arithmetic while preserving operator history and state.
All 12 native contracts pass and reference PCM is identical. Static data/BSS
and linker heap remain unchanged. Installation changed 137 sectors and complete
readback matched the packaged image above. Reset/observation passed on COM4:
stage4/fault0, zero LCD/key errors and frames19→53.

Actual UAC recording `build/usb-audio-opl-silent-fast-capture.wav` contains
881,868 frames / 19.997 seconds of 44,100 Hz stereo PCM16. PCM SHA-256 is
`a49e4e137c2a6ac3e4bac8df21ba60d10b96297890fc2739488bc32ad907a1ab`.
Left/right peaks are7383/7385 (-12.94 dBFS), RMS1182.63 (-28.85 dBFS), with zero
fullscale endpoint samples. Its 23.047-second live observation stayed in OPL
mode0, with sfx_started3 fixed and sfx_voices0 throughout: the sustained capture
is music-only, excluding background effect playback as its noise source.
IRQs27201→42717 and outputframes1740864→2733888 give673.23 IRQ/s and
43,086.91 stereo frames/s, below the configured44,100 frames/s. IIS remained
ready1/error0, ADC errors0 and heap_free4244; raw/gain203/24 fell to0/0.
The final live USB row records sent996947/silent26456/under2157/over1149 and
starts2/stops7. These cumulative counters include Windows stream reopening;
the saved WAV alone does not establish continuous audio acceptance.

The paused-engine comparison in `build/opl-silent-fast-render-paused.jsonl`
kept entered/completed4951 unchanged and capture inactive. Over9.125 seconds,
IRQs306745→312905 and outputframes19631680→20025920 give675.07 IRQ/s and
43,204.38 stereo frames/s. The rate near43,200 suggests a possible actual IIS
clock mismatch with the configured44,100 Hz. A later captured PAUSE test on
the monitor image reaches approximately 44,100 frames/s with engine and MUS
generation stopped, disfavoring a fixed 43,200 Hz clock explanation.
Producer counters do not themselves validate missed interrupts or establish
renderer deadline misses as the sole cause; this removes both loads together.

`DOOM MUTE 1` was acknowledged and telemetry showed speaker_muted1/gain0.
The physical hiss observation is pending. Source audit found no effect-volume
bypass, signedness/rate or stale-playback defect; music and effects share the
master gain. The remaining music noise has not been attributed to a source.

### Music-monitor image: installed, warm boot observed

Application 561,520 B, SHA-256
`6150df022dd98f525ea8fa59fa67020a4d0a9f62331be79a3d123ddd9cf15c7f`;
packaged full-image SHA-256
`71d2cc0d86965ac4ad146ba7493ec2eca2e43a1d82cf12f5236c441753bcf461`.
Flashing completed with full readback matching `71d2cc0d...`. Warm-reset
observation on COM4 reached stage 4/fault 0, zero LCD/key errors and frames 20→54.
Physical noise and cold-boot USB acceptance remain open. All 12 native contracts
pass. The default FULL path
matches 37,784 original OPL register writes and 4,233,600 reference frames byte
for byte. A lazy drum-history ownership mask corrects a one-LSB discrepancy
at the first crossfade after stem extraction resumes.

`DOOM MUSIC FULL/MELODY/DRUMS/PAUSE` provides reversible listening isolation.
MELODY/DRUMS retain normal score/voice/operator state; PAUSE freezes music
generation. Temporary modes restore FULL after 15 seconds, protocol reset/unplug
or fragment timeout. Percussion uses original OPL GENMIDI patches, not the PCM
effect bank; both percussion and effects obey the common master gain. The
previous music-only UAC capture still contains OPL percussion, so zero effect
voices alone does not resolve the user's drum-noise suspicion.

Static storage grows by 16 B: 26,896 B data + 451,528 B BSS = 478,424 B;
linker heap remains 45,132 B. The 1,472 B diagnostic chain plus 1,024 B SDK
margin fits the 2,560 B USB stack with 64 B remaining. These are build gates;
they do not establish physical listening acceptance.

The captured PAUSE interval holds engine entered/completed at 1515 and MUS
ticks unchanged. Device trace over 4.95 seconds measured 44,114.75 stereo frames/s
and 689.29 callbacks/s; host-counter regression measured 44,098.63 frames/s.
No capture silent/under/over counts were added in that window. FULL/MELODY/DRUMS
still accumulated transport corrections in separate song intervals. Windows
capture retains residual +/-1 samples during PAUSE and small stereo differences;
it is not a bit-exact device PCM fixture. Removing both engine and music loads
disfavors the earlier fixed 43,200 Hz IIS-clock hypothesis without proving
continuous output timing or isolating which work delayed production.

The phone recording analysis covers 6.25 seconds of decoded AAC at 48 kHz, with
an initial 0.491-second zero region, no interior exact-zero gaps and no decoded
clipping. Noise bursts in 2–6 kHz differ by about 24.7 dB between louder and
quieter regions. This supports investigating drum/transient content, without
proving PCM rather than OPL or excluding analog noise/phone gain processing.
Physical drum isolation remains pending.

The user reports normal cold boot showing Doom while USB interfaces are absent;
Windows saw no application serial/UAC device, then manual UBOOT was visible.
Cold-boot USB enumeration remains unresolved. SDK USB PLL initialization is
present and preserves the CLK_CON1 source selector. The clock hypothesis has
not been proved as the cause of the missing application interfaces.

### Current DMA-output image: installed, speaker improvement reported

Application 563,440 B, SHA-256
`ce8176d84946623efc296434973dbabc75c094edd3b7a8cb8ea29fbc39762ab3`,
was flashed and full readback matched image SHA-256
`2a432d0f469a21169a5cbe638fb2c5f95e9be493845eadd2eda0cd92951a6a1b`.
Warm-reset observation on COM4 reached stage 4/fault 0, zero LCD/key errors
and frames 109→142. The user reports that speaker output is improving and
almost perfect; complete physical noise acceptance remains open.

A private 512 B scratch block completes music/effects rendering, the premaster
USB tap and common master scaling before a single `memcpy` to the SDK DMA half
followed by target `csync`. A regression fails on the prior direct-render path
and passes when DMA contents remain unchanged during rendering. This proves
the software ownership change, not a physical noise or missed-deadline fix.
Seven hot helpers plus `update_noise` are forced inline with exact reference
PCM retained.

Static RAM 479,000 B, linker heap 44,556 B and reviewed startup reserve 4,544 B
pass the build budgets. Optional F4-at-boot LCD USB-register diagnostics use
60 B snapshot storage and the existing LCD strip, perform read-only inspection
and expire after 30 seconds. The user subsequently reports cold-boot USB still
disappears. Windows found neither application nor UBOOT devices before capture
opened, so that attempt produced no recording. Exact transition timing is
unconfirmed; this is not proof of a spontaneous firmware hang. New capture,
runtime audio measurements and F4 LCD evidence remain pending.

### Host PCM: complete score, exact and unclipped

Ignored `build/host-bgm-pcm-check/` contains a repository-source 97-second E1M1
WAV at 44,100 Hz/stereo PCM16, OPL mode 0/FULL, default music/effects 64 and no
active PCM effects. It covers the 96-second score plus one second after looping,
with source hashes verified unchanged after rendering. A second WAV uses the
real board startup/master-127 implementation.

Premaster range -7516..2976 gives peak -12.7893 dBFS, RMS -29.0226 dBFS and
DC -757.4211 PCM16. Quarter-limit 8191/-8192 endpoints, strictly clipped samples
and fullscale endpoints are all zero. The first 4,233,600 frames and 37,778
ordered register writes at constant 64 match the unchanged independent original
driver/core exactly; the 37,784-write regression also changes volume 127/64.
The largest peak follows percussion note 57 by 1.678 ms. The largest sample
step also matches the reference and is not at a 64-frame block boundary.
No added clipping, overflow or discontinuity was identified in the compared
music. This synchronous offline result cannot measure analog noise or device
DMA/USB underruns; physical audio and cold-boot USB acceptance remain open.

USB packet commits are bounded and epoch-checked; busy/rejected packets preserve
pending samples. CDC uses the queued reply's generation, and linked validation
rejects SDK polling packet writers. Capture-only UAC1 uses interfaces 2/3 and
EP0x81 at 44,100 Hz, stereo PCM16. The corrected first door, CPU1 audio ownership,
and exploration beyond that door remain established from `8a36a90`; complete
E1M1 traversal and 30 displayed FPS acceptance remain open.

## Earlier bring-up evidence

The following milestones are historical; their pending actions and port names
describe observations at those checkpoints.

The OPL/synth/knob revision `3318678` was installed and readback-verified, but
the user reports silence and USB requests fail after cold boot. An emitted
3,192 B USB task frame plus status formatting reaches 4,320 B, exceeding its
4,096 B allocation. The source repair keeps mutually exclusive reply buffers
in separate frames: task 136 B, maximum diagnostic chain 1,424 B. The builder
now gates final ELF diagnostic frames with a further 1,024 B SDK margin.
The `58b9ac4` repair was flashed and complete-readback verified, SHA-256
`71a78229cec1355d89f95d0a361cfcc751f07571397c0acd28f95b1480d61e2d`.
USB observation reached stage 4 with zero engine/LCD/key errors and device
frame capture worked. Audio reported `ready=0`, `error=-1`, `irqs=0` and
`volume_valid=0`, exposing an IIS initialization failure.

The installed `03492fc` allocation fix uses a 3,072 B USB stack to return 1 KiB
to the runtime heap. App size is 555,024 B, with the same 170,505 B archive,
477,912 B static RAM and 45,644 B linker heap. Reviewed tasks/queues/idle use
37,464 B and reviewed startup reserve is 5,120 B before other allocations.
Its diagnostic chain is 1,436 B plus 1,024 B SDK margin, leaving 612 B. The
explicit `heap_free` diagnostic queries aggregate remaining allocator space,
not the largest block or minimum-ever free heap. Python 71/71, target link,
native USB protocol and 424 source/config/header closure hashes pass.
The candidate is now flashed with complete readback SHA-256
`d2ed5405c1e1c24e8fc75bb77004d59e77a1d25035681953370a1e40a205ae76`.
One reset completed, but HELLO observation failed while COM10 enumerated.
The user now confirms audible music, no knob response and apparently hung USB.
Windows currently sees no FM-1 serial port; fresh diagnostics await reconnection.

The next candidate binds Doom/USB tasks to CPU0 and ALINK only to CPU1 at
priority 3, with CPU0's ALINK route masked and teardown on CPU1. PB6 ADC4 is
polled every two milliseconds by the existing CPU0 key timer under a separate
volume lock; a volatile Q7 target reaches the audio callback without ADC work.
USB scalar audio snapshots bypass the mixer lock and may straddle callbacks.
`DOOM AUDIO` adds `volume_target` and `volume_samples`. Python 72/72 and three
native USB/sound/volume-integration contracts pass, including actual ADC-to-
output full/half/zero/restored gain and timeout. Installation and physical
knob/USB acceptance are pending; USB-starvation causality remains unproved.
The new target link passes after fresh compilation of all 79 engine and seven
port sources. App size is 555,120 B, static RAM 477,896 B, linker heap 45,644 B
and reviewed reserve 5,120 B. Its 1,460 B diagnostic chain plus 1,024 B SDK
margin leaves 588 B in the 3,072 B USB stack. Reconnection, physical UBOOT and
a fresh protected writer session are needed; the old failed-observation latch
is preserved.
Historical device evidence follows.

The spawn-area report was investigated with both installed and compacted
assets at a 296 KiB host zone. One F5 press near the first door opens sector 4,
and the player reaches sector 52 where enemies attack. Straight movement from
spawn normally hits the north wall; the room exit is to the right. Complete
E1M1 map lumps are unchanged. A physical capture shows the spawn room and
right-hand opening; a capture at the reported stopping location remains needed.

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
| Earlier menu Doom-only size probe | Links without NES app; 361,072 B app sections before WAD; 470,728 B static RAM; 52,812 B linked heap span; 478,662 B app plus FMD1 | Inert app and unsupported libc; size baseline only |
| Revised UBOOT `app.bin` input | Links Doom/USB tasks, XIP FMD1, shared LCD DMA strip and paced SPI2 scanner: 504,592 B app, 474,264 B static RAM, 49,292 B linked heap; reviewed startup reserve 4,672 B | Offline budget passes; physical runtime heap/stack still unmeasured |
| Startup and video audit | Correct SDK stack units; static bounded inflater; zone-backed lumpinfo; remove 48,000 B wipe peak; save 6,768 B drawing RAM; 64× fewer gameplay palette samples | Complete pixel reference comparison passes; this is not a whole-frame speedup claim |
| Audited host/decoder tests | Normal CTest 5/5, lowres x86 CTest 4/4, Python 21/21; 114 archive blocks decoded; 296 KiB x86 moving/menu/restart/exit pass | SDK headers with desktop zlib for decoder execution; target SDK compile/link pass |
| Earlier eight-row host output | 4,860 callbacks over 120 ticks; exact image hash match with four-row output | No hardware timing or FPS result |
| First FM-1 Doom flash | 121 sectors written; complete 1 MiB readback and decoded application/CRC match | User reported blank LCD after reset and cold boot; display/input acceptance failed |
| Doom CDC startup diagnosis | Second flash verifies 124 sectors/full readback; reset and CDC boot work, zero LCD error; fatal config name lacks its `%i` index | Generated printf normalization fixes the confirmed SDK incompatibility; serial UBOOT recovery passes |
| Corrected physical Doom boot | Six changed sectors/full readback match, reset succeeds, stage 4 and frames 133→167 with zero faults; user confirms visible gameplay | Controls still being checked; approximately 11 counter increments/s in short capture, not 30 FPS acceptance |
| Scanner freeze and graphics correction | 569,840 B app; 474,488 B static RAM; 49,068 B linker heap and 4,448 B reviewed reserve; 140 changed sectors/full readback match; reset/CDC succeeds | Whole-map memory/stack and 30 FPS remain open |
| Revised graphics and memory tests | Original pistol/punch, 4×4 world/enemies, Smooth/Detailed toggle; host600 firing/moving ticks and Detailed120 pass at296 KiB; CTest5/5 and4/4, Python30/30, all145 archive blocks decode | Host tests plus target compile/link; audio disabled |
| Sustained physical input after fix | User confirms stable movement/firing and both views; 60-second Detailed-mode capture frames629→1292 with zero engine/LCD/key errors, retry0 and advancing scanner/IRQ counters | Approximate11.05 counter increments/s; not distinct-frame30 FPS acceptance |
| Original sound-effect backend | Two streaming IMA ADPCM voices mixed into stereo 24-bit IIS output; host decoder, clipping, bounds and lifecycle tests pass; user confirms audible effects | Sound quality and comprehensive DMA deadline acceptance remain open |
| Original E1M1 music backend | Eight integer voices; original 140 Hz note/controller timing, looping, pause/stop and bounded decoding; exact local MUSX event comparison and host module tests pass; user hears music | Simplified instruments; original peak ten distinct notes can require voice stealing; balance/timbre remains under review |
| Audio ROM correction | Initial audio link 595,216 B and intermediate 593,488 B both exceed the 584,956 B slot; integer formatter/parser and local `strdup` reduce SDK closure | Neither oversized intermediate is a flashable candidate |
| Audio diagnostics and memory review | USB 1024 words/4 KiB; IIS 1024 B DMA included in startup budget; XIP game-data banks and static music state; measured USB minimum unused stack 378 words/1512 B and maximum reported IRQ 500 µs | IRQ measurement has 500 µs quantization and does not detect underruns; runtime heap/other stack headroom remain unmeasured |
| Final audio application | 583,216 B app with 1,740 B slot headroom; 177,331 B graphics archive; 475,176 B static RAM; 48,364 B linker heap; 6,816 B reserve after reviewed startup | Installed and booted; historical 569,840 B controls milestone remains separate from new-image physical movement acceptance |
| Final audio verification | 43 Python tests and four focused audio/protocol/formatter CTests pass; all 411 source/config/header hashes match | Host/compile/link evidence; sound-quality and whole-level acceptance remain open |
| Audio flash/readback/boot | 143 changed sectors, directory 0x4000 last; full 1 MiB readback verified; reset/COM6 DOOM-FM1/1 stage 4, frames 149→182, zero engine/LCD/key errors | Current audio shutdown and runtime heap remain open |
| Sustained installed audio observation | 105-second Detailed-mode capture: frames 1084→2220 over 104.078 s (~10.91489 counter increments/s), audio IRQs 69160→140162, output frames 4426240→8970368, loops 1→2; ready/playing 1, audio/music errors 0, scanner failures/retries 0 | User confirms music/effects but requests balance/timbre improvements; not new movement confirmation or 30 distinct frames/s acceptance |

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
historical tracker v31 candidate uses 381,192 bytes of `.ram0_bss` and leaves a
131,948 byte heap according to its offline static audit; those figures describe
that image, not this Doom port. Doomgeneric's host allocator is not a target
memory plan. Its normal game-data path uses `fopen`/`fseek` on an IWAD.

The [RP2040 Doom port](https://github.com/kilograham/rp2040-doom) demonstrates
that a reworked original engine can fit 264 KiB of RAM by keeping immutable
level data in flash. Its [memory notes](https://kilograham.github.io/rp2040-doom/speed_and_ram.html)
describe why a straight Chocolate Doom build does not fit. The historical
graphics/input milestone uses a 181,995 B E1M1 archive with original pistol/punch
and finer world/enemy art, linked in read-only XIP flash with a 4 KiB cache.
Its 569,840 B app fits the reviewed 584,956 B slot. Its fixed 296 KiB zone,
4 KiB cache and 7 KiB bounded inflater arena are included in 474,488 B static
RAM, leaving a 49,068 B linker heap span. The historical `fc03a11` audio image additionally
implements original sound effects and E1M1 music, accounts for 1024 B IIS DMA,
and exposes USB stack/IRQ telemetry. Its final 583,216 B app embeds a 177,331 B
archive with the original pistol, 2×2 fist and 4×4 world/enemies. Static RAM is
475,176 B (`.ram0_data` 26,800 B plus `.ram0_bss` 448,376 B); the linker heap is
48,364 B. Reviewed tasks/idle use 38,488 B, plus 800 B initialization, 1,236 B
USB and 1,024 B IIS requests, leaving 6,816 B before other allocations/padding.
The required heap floor including the 4,096 B reserve is 45,644 B.
The application SHA-256 is
`715f58b29fd2f0280ead27adf975aa5908b6832b5b6ebf92634ca7426a9927a5`.
Its full 1 MiB readback SHA-256 is
`ce9544e7730372690c9912a83d17ae73d641406c9153d26ada64ce3a7eea6ca9`.
That milestone's music/effects were audible, USB stack telemetry and sustained
progress were recorded, and balance/timbre remain under review. Runtime heap, other
stack headroom, current audio shutdown and whole-level acceptance remain open.
See [TARGET_CANDIDATE.md](TARGET_CANDIDATE.md) and
[AUDIT.md](AUDIT.md).

## Required before calling this playable on FM-1

1. Extend established physical boot/XIP evidence to whole-level traversal and
   corruption/error handling.
2. Exercise the fixed zone under the linked SDK Doom task and prove SRAM,
   stack, heap, and protected-flash budgets from live diagnostics and the ELF.
3. Complete sound-quality, DMA deadline and current audio-shutdown checks;
   retain established LCD/input and recovery behavior. Music/effects are
   audible in the earlier milestone; physical OPL music volume control is now
   confirmed, while remaining noise and current audio timing require acceptance.
4. Reproduce the host gameplay image through the LCD, then measure controls,
   frame rate, WAD latency, audio, battery behavior, and restart on the bench.
5. Preserve the known-good installed v32 image, verify a candidate and restore
   plan, and perform any physical write only as a deliberate bench step.

Until these gates pass, the generated private `build/target-candidate/app.bin`
is a UBOOT application input, not a hardware-qualified Doom release or a full
flash image. No game data or firmware binary is committed to the repository.
