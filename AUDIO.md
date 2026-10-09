# FM-1 Doom audio

## NES FX bank source (2026-10-08)

Source `783a03d` adds the NES port's filter/Wah effect alongside the retained
synth editor. It has **not been flashed**. All 13 native contracts pass,
including the exact original OPL reference; shared NES FX tests also pass.
Target build, accepted-profile comparison and budget gates pass. Independent
code audit passed; hardware acceptance remains pending. The shared prepared
NES API is board-project revision `2a577e8`.
The installed firmware is still the `70553874...` image recorded below.

SELECT switches Synth bank0/NES FX bank1 after four net contact edges. Each
bank retains its own settings. Bank selection does not change the OPL/synth
source; E4 remains its toggle. Returning to Synth bypasses NES FX while keeping
its values. Boot defaults are Original OPL, Synth bank and bypassed NES FX.
The Synth bank's presets, algorithm and VCO/VCF/VCA/reverb controls are unchanged.
Synth selectors wrap; NES FX preset and algorithm selectors clamp at 0..3.

| Control | NES FX assignment | Range |
| --- | --- | --- |
| PRESETS | Dry, Warm, SlowWah, AcidSweep | 0–3 |
| ALGORITHM | Bypass, low-pass, band-pass, high-pass | 0–3 |
| KNOB1 | Cutoff | Raw 0–127, logarithmic 80–5,000 Hz |
| KNOB2 | Resonance | 0–127 |
| KNOB3 | LFO rate | Raw 0–127, 0.1–12.8 Hz |
| KNOB4 | LFO depth | 0–127 |

The exact shared NES recipes are:

| Preset | Algorithm | Cutoff | Resonance | Rate | Depth | Mix |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Dry | Bypass (0) | 112 | 0 | 19 | 0 | 127 |
| Warm | Low-pass (1) | 92 | 24 | 9 | 0 | 127 |
| SlowWah | Low-pass (1) | 76 | 60 | 9 | 72 | 127 |
| AcidSweep | Band-pass (2) | 84 | 100 | 29 | 96 | 100 |

NES FX processes the complete mono music, including OPL drums, before the
independent PCM gunshot/effects mixer and physical master gain. The existing
-8192..8191 music clamp retains effect headroom. The synth room effect still
uses the same 2 KiB delay; NES FX adds one 36 B state and 9 B control structure,
with no extra audio queue or sample buffer.

The shared `fm1_nes_fx` setter prepares the LFO phase increment in task context,
including its 64-bit division when rate changes. The IRQ uses only prepared
state and exact signed 32-bit split products; its render contract excludes
64-bit arithmetic, allocation, I/O and waits. Prepared-render target IR uses
only 32-bit arithmetic; the 64-bit division remains in task preparation.
Shared formal tests compare 226,084 samples with the exact 64-bit reference,
all 128 LFO rates, stale-rate rejection and prepared/in-place/partition/single
sample paths. These checks do not establish audible hardware response or IRQ
deadlines. `DOOM EDIT` remains read-only: `bank=0` reports
VCO/VCF/VCA/reverb and `bank=1` reports cutoff/resonance/rate/depth. Both report
preset/algorithm and the actual `synth_mode` independently of the bank.

Before comparing target profiles, verify the private archive and banks against
the last accepted manifest. The correct retained archive is the 170,636 B
`build/fine-assets/audio-door-fixed.fmd` (`197d7ccf...`); the old default filename
contained an 8×8 archive. See [target profile](TARGET_CANDIDATE.md#incremental-bench-profile).

The unflashed candidate is 567,344 B, app SHA-256 `4f3871be...`, with 17,612 B
flash margin, 479,208 B static RAM, 44,332 B linker heap and 4,320 B reviewed
startup reserve. The USB diagnostic chain is 1,480 B plus a 1,024 B SDK margin,
leaving 56 B in its 2,560 B stack. Full hashes and build gates are recorded in
[TARGET_CANDIDATE.md](TARGET_CANDIDATE.md#offline-nes-fx-artifact-2026-10-08-unflashed).
The additional candidate/stack Python checks pass 18/18. Offline request
validation preserves boot/config/tail with a 139-sector scope and directory
last; it establishes no device write or new readback.

## Last installed live editor (2026-10-07)

The live editor from source `626d2612` is installed: **565,744 B**, app SHA-256
`70553874...`, with complete image readback matching `a4c3fc49...`. After one
reset, IIS reported `ready=1/error=0`; audio frames advanced 2,578,432→2,977,216
and music ticks 8,065→9,331 with zero music errors. Read-only `DOOM EDIT`
confirmed Original OPL mode and the default parameters. The volume ADC was
valid with zero errors and raw 259..260/target gain 32; no knob sweep was made.
All 13 native contracts and linked RAM/flash/stack gates pass. Earlier images
established music/effects and physical master volume, with near-perfect speaker
output reported by the user. Physical acceptance of this image's controls,
speaker output and callback timing remains pending. See the
[deployment record](DEPLOYMENT.md#live-editor-deployment-2026-10-07).

The apparent cold-boot USB failure was resolved when the user replaced the
programmer cable with a normal USB data cable. Windows then exposed healthy
COM5 and a UAC recording endpoint, and read-only observation showed advancing
gameplay with zero LCD/key errors. This established a cable-related connection
issue; a firmware cold-boot defect was not proved. The cable's electrical
mechanism was not measured. Historical missing-device captures below are
superseded by this finding.

Music still boots in the exact original DOS OPL mode. The optional eight-voice
melodic synth adds live parameters through the shared NES encoder scanner:

| Control | Assignment | Default / range |
| --- | --- | --- |
| PRESETS | Original, Warm, Acid, Room | Original / 0–3 |
| ALGORITHM | Classic, Triangle, Pulse, Mixed band-pass | Classic / 0–3 |
| KNOB1 | VCO detune, up to approximately 1.5% for the second oscillator | 16 / 0–127 |
| KNOB2 | VCF cutoff, retaining the melodic envelope sweep | 72 / 0–127 |
| KNOB3 | VCA contour; raising it lengthens attack and release | 32 / 0–127 |
| KNOB4 | Small-room reverb wet amount | 0 / 0–127 |

Selecting Warm, Acid or Room enables synth mode; Original selects OPL. Warm
retains the melodic synth character, Acid increases the filter sweep, and Room
softens the filter and lengthens the envelope. E4 toggles OPL/synth using the
last nonzero preset recipe and retained knob values. Unchanged-preset knob or
algorithm edits preserve the E4 mode choice. `DOOM EDIT` is read-only feedback:
it reports `preset`, `algorithm`, `vco`, `vcf`, `vca`, `reverb` and actual
`synth_mode`; it does not modify the controls.

Parameters and waveform weights smooth once per 64-frame mixer block. Notes
and score timing continue through changes. The game task copies completed
encoder counts, releases the scanner lock, then applies edits under the existing
audio lock; scanner interrupts do no synthesis, rendering or allocation.

Reverb treats only the analog melody before original OPL percussion is mixed.
It uses a caller-owned **1,024-sample mono PCM16 delay (2 KiB)**, normalized
reflection taps, filtered feedback of 17,000/32,768 (about 0.519), and a convex
dry/wet mix. Wet amount reaches approximately 50% in Warm/Acid and 75% in Room;
zero bypasses it exactly. Delay processing averages four input frames at
11,025 Hz and interpolates the filtered wet output to 44,100 Hz. This is a small
room effect. No heap allocation is added: the existing fixed 296 KiB allocation
is partitioned into a **294 KiB engine zone plus the 2 KiB delay**. New music
parameter state is approximately 63 B before target alignment. The linked
candidate has 479,160 B static RAM, 44,396 B linker heap and a 4,384 B reviewed
startup reserve, above the 4,096 B gate. Its 565,744 B app fits the 584,956 B
slot. The emitted USB diagnostic chain is 1,476 B; adding its 1,024 B SDK
margin leaves 60 B in the 2,560 B USB task stack.

Original OPL drums, lossless PCM effects, PB6/ADC4 physical master control,
premaster UAC tapping and private completed-block DMA output keep their existing
paths. Editing synth controls in settled Original mode does not change the
music output.

Host music and original OPL reference contracts pass. Coverage includes control
bounds and mode ownership, unchanged original-mode PCM during control sweeps,
wet-zero byte-exact bypass, guarded delay sizes and oversize/invalid buffers,
audible changes from each knob/algorithm/preset, a post-release room tail,
unchanged OPL percussion, eight-voice rapid sweeps and release/tail convergence.
The independent OPL reference still matches **37,784 ordered register writes
and 4,233,600 output frames**. These are source/host results, not physical
acceptance of the new controls or proof of target callback deadlines.

The installed image exposes healthy CDC and UAC devices. UAC telemetry reported
`armed=1/active=0`; no recording was made, so endpoint presence does not establish
streaming acceptance. The observed maximum audio IRQ duration was 2,000 µs,
above the nominal 64-frame/44.1 kHz interval of about 1.451 ms; sustained deadline
acceptance remains open. Receipts and runtime stack/heap values are recorded in
[deployment evidence](DEPLOYMENT.md#live-editor-deployment-2026-10-07).

## Historical bench investigation (2026-10-06–07)

The following records retain the image-specific investigation. In particular,
older statements about missing cold-boot USB predate the cable finding above.

The UAC/headroom revision, `60827f7`, was flashed and complete-readback
verified. Its application SHA-256 starts `71cea5ce`; the full image starts
`ae085194`. IIS reported `ready=1`, `error=0`, advancing callbacks and aggregate
`heap_free=4244`. UAC recording produced 19.779 seconds of 44,100 Hz stereo
PCM16, peak -13.70 dBFS and RMS -31.08 dBFS. That image had substantial underflows:
the measured producer supplied about 33,454 stereo frames/s against 44,100
required. This establishes recording, not continuous real-time playback.

A confirmed physical knob sweep still returned `raw=0` throughout 26 snapshots,
with samples30248→35552, valid1 and errors0. The exact older CDC NES reference
was then flashed and readback-verified on the same device: ADC readings covered
0..1023 with zero errors, and the user confirmed the knob works. The hardware
and shared ADC driver therefore work in that reference. The current image's
input and gain now respond as described below; the precise failing integration
step was not isolated. See [bench evidence and image hashes](PORT_STATUS.md).

The earlier early-ADC image has application SHA-256 starting `998677b7`
and full-image SHA-256 starting `f328adbc`. It starts the unchanged volume
driver before registering the scanner timer, matching legacy CDC NES/MDX,
and preserves its primed samples, accepted target and error state through
sound initialization. It adds no static RAM. It was flashed with 137 changed
sectors and full readback matching `f328adbc...`; reset and observation reached
stage4/fault0, frames129→163 and zero LCD/key errors. At that checkpoint the user reported no
knob control. A 3.84-second initial capture showed raw221..222, accepted222,
target=gain27, errors0, samples11030→11938, IIS ready1 and heap_free4244.
The nonzero reading does not prove working control: earlier ADC startup did
not resolve the reported failure.

The earlier volume-control image has application SHA-256 starting `909c0188` and full-image
SHA-256 starting `ac0af0b6`. It uses IIS open/rate → ADC → ALINK before LCD and
scanner initialization, matching the newer UAC NES hardware startup order.
The existing 1 ms timer also starts before LCD setup and polls ADC every 2 ms;
scanner work stays disabled until setup completes. Seven hot OPL helpers are
forced inline, with exact reference output retained. All 12 native contracts
pass and static RAM is unchanged. Flashing changed 138 sectors and full readback
matched `ac0af0b6...`; reset/observation reached stage4/fault0, frames19→53 and
zero LCD/key errors.

A confirmed approximately 25.5-second physical knob sweep recorded raw and
accepted ADC0..1023, target and gain0..127, errors0 and samples28174→37836.
IIS remained ready1/error0 and heap_free4244. The grouped startup/polling change
restores ADC-to-gain response; it does not isolate a single register or clock
interaction. The user confirmed OPL music loudness follows the knob correctly.
They still report noise during music, especially at low master volume;
its source has not been isolated to synthesis, PCM effects or output timing.

All 37 AUDIO snapshots in the 25.485-second window stayed in OPL mode0.
Measured callback rate was 670.79 IRQ/s and production 42,930.35 stereo frames/s,
improved from the earlier 33,454 but below 689.06 IRQ/s and 44,100 frames/s required.
The cumulative max_irq_us3500 includes earlier synth mode1 activity and is not
an OPL-only maximum. That image's new recording attempt could not start because
Windows no longer exposed serial
or UAC at that moment. Windows subsequently detected a healthy UBOOT device;
the missing application interfaces do not establish a runtime USB hang or its
cause. The earlier `60827f7` recording remains verified.

An earlier readback-verified image is a 561,040 B application with SHA-256 starting `8d7fc70d`
and full-image SHA-256 starting `50d6ee10`. It retains the verified startup
changes and adds an exact OPL silent-operator path: when eg_out>=EG_MAX,
waveform, AM and feedback arithmetic are skipped while operator history and
state remain intact. All 12 native contracts pass and reference PCM remains
identical; static RAM is unchanged. Installation changed 137 sectors and full
readback matched `50d6ee10...`; reset/observation reached stage4/fault0,
frames19→53 and zero LCD/key errors.

Its actual UAC recording is 19.997 seconds (881,868 frames) of 44,100 Hz stereo
PCM16, PCM SHA-256 `a49e4e137c2a6ac3e4bac8df21ba60d10b96297890fc2739488bc32ad907a1ab`.
Peak was -12.94 dBFS and RMS -28.85 dBFS, with no fullscale endpoint samples.
Every captured AUDIO snapshot had OPL mode0, sfx_started3 unchanged and
sfx_voices0: this sustained recording contains music without PCM effects.
The 23.047-second live window measured 673.23 IRQ/s and 43,086.91 stereo frames/s.
IIS remained ready1/error0, volume errors0 and heap_free4244; knob raw/gain fell
from 203/24 to 0/0. USB diagnostics still accumulated silent/under/over counts;
the final row showed sent996947/silent26456/under2157/over1149 and starts2/stops7.
Those cumulative counters include Windows stream reopening and do not establish
continuous audio acceptance from the saved WAV alone.

With engine rendering deliberately paused and capture inactive, a separate
9.125-second window measured 675.07 IRQ/s and 43,204.38 stereo frames/s.
Engine entered/completed stayed4951. The rate near 43,200 suggests a possible
IIS clock mismatch with the configured 44,100 Hz. A later captured PAUSE test
reaches approximately 44,100 frames/s with engine and music generation stopped,
disfavoring a fixed 43,200 Hz clock explanation.
The lower producer count is not a validated missed-IRQ counter and does not
establish renderer deadline misses as the sole cause; removing both loads
does not isolate which work delayed production.

`DOOM MUTE 1` was acknowledged and telemetry confirmed speaker_muted1/gain0.
Whether physical hiss persists after digital mute still awaits the user's
observation. The reported noise has not been assigned to a specific source.

The new music-monitor build is a 561,520 B application, SHA-256
`6150df022dd98f525ea8fa59fa67020a4d0a9f62331be79a3d123ddd9cf15c7f`,
packaged full-image SHA-256
`71d2cc0d86965ac4ad146ba7493ec2eca2e43a1d82cf12f5236c441753bcf461`.
It was flashed and full readback matched `71d2cc0d...`. Warm-reset observation
on COM4 reached stage 4/fault 0, zero LCD/key errors and frames 20→54.
Physical noise and cold-boot USB acceptance remained open at that checkpoint. All 12 native
contracts pass, including 37,784 original register writes and 4,233,600
byte-identical reference frames. The default FULL path preserves that reference.

Temporary `DOOM MUSIC FULL/MELODY/DRUMS/PAUSE` commands select the complete mix,
melody only, drums only, or frozen music generation. Isolation modes expire
after 15 seconds and return to FULL on protocol reset/unplug or fragment timeout.
Melody/drum selection preserves score events, operators and voice allocation;
it does not substitute percussion samples. A lazy OPL drum-history ownership
mask fixes a one-LSB first-crossfade discrepancy when drum extraction resumes.
The build adds 16 B static storage: BSS 451,528 B, total static 478,424 B, with
45,132 B linker heap unchanged. Its 1,472 B diagnostic chain plus 1,024 B SDK
margin fits the 2,560 B USB stack with 64 B remaining.

Captured monitor PAUSE diagnostics kept engine entered/completed at 1515
and music ticks unchanged. Device trace over 4.95 seconds measured
44,114.75 stereo frames/s and 689.29 callbacks/s; host-counter regression
measured 44,098.63 frames/s. That window added no capture silent/under/over
counts. This disfavors the earlier fixed 43,200 Hz IIS-clock hypothesis.
FULL, MELODY and DRUMS still accumulated transport corrections in their
separate song intervals. Windows capture has residual +/-1 samples during
PAUSE and small stereo differences; it is not a byte-exact device fixture or
a same-interval drum/melody comparison. Physical noise is not thereby resolved.

### Historical DMA-output image: installed, speaker improvement reported

Application 563,440 B, SHA-256
`ce8176d84946623efc296434973dbabc75c094edd3b7a8cb8ea29fbc39762ab3`,
was flashed and full readback matched image SHA-256
`2a432d0f469a21169a5cbe638fb2c5f95e9be493845eadd2eda0cd92951a6a1b`.
Warm-reset observation on COM4 reached stage 4/fault 0, zero LCD/key errors
and frames 109→142. The user describes speaker output as improving and almost
perfect; this is improvement reported by the user, not complete noise acceptance.

The image renders into a private 512 B scratch block, taps that premaster block
for USB, applies the common master gain privately, then copies the completed
block to the SDK DMA half and issues target `csync`. A regression that checks
DMA contents during sample generation fails on the earlier direct-render path
and passes with private rendering. This establishes the software ownership
change, not physical elimination of noise or missed deadlines. Seven hot
operator helpers plus `update_noise` are forced inline with exact reference
PCM retained.

Static RAM is 479,000 B, linker heap 44,556 B and reviewed startup reserve
4,544 B. Optional F4-at-boot LCD diagnostics add 60 B of register-snapshot
storage, reuse the existing LCD strip and expire after 30 seconds. They read
USB registers without reconfiguring them. After warm-boot observation the user
reported that cold-boot USB still disappears; Windows subsequently exposed
neither application nor UBOOT devices, so the next UAC capture did not record.
The precise transition timing is unconfirmed and does not establish a spontaneous
firmware hang. New runtime audio/capture measurements and F4 LCD evidence remain
pending at that checkpoint; no cold-boot USB fix was claimed for this image.

The user's phone video was analyzed as a 6.25-second decoded AAC recording at
48 kHz. It had an initial 0.491-second zero region, no interior exact-zero gaps
and no decoded clipping. Noise bursts in the 2–6 kHz band changed by about
24.7 dB between louder and quieter regions. This supports investigating drum
transients; it does not establish sampled PCM percussion or exclude analog
noise, microphone processing or phone automatic gain control. Drum isolation
has not yet established the noise source on the FM-1.

The user separately reports Doom visible after a normal cold boot while USB
interfaces are absent; Windows also saw no application serial/UAC device.
Manual UBOOT was subsequently visible. Cold-boot USB enumeration was then
unresolved, before the cable finding above. The pinned SDK's USB PLL initialization is present and preserves
the CLK_CON1 source selector; a clock initialization cause has not been proved.

The proposed internal-reference/VBG probe is not implemented: SDK P33 locking
and SPI-transfer waits are unbounded, with no verified try-lock, cancellation
or restoration path. These revisions change no P33 gates or reference settings.

Capture-only UAC1 at 44,100 Hz, stereo PCM16 taps the music/effects mix before the
physical master gain, so it can record while a zero knob reading mutes the
speaker. `DOOM MUTE 1/0` controls an optional speaker override, and `DOOM AUDIO`
reports its state. `DOOM VOLUME` provides bounded read-only register snapshots.

USB capture handles busy/rejected submissions with persistent staging and an
epoch check; CDC submits using the queued reply's generation. Neither endpoint
uses the SDK polling packet writer. The first flashed UAC image enumerated
serial and recording endpoints after UBOOT reset, but IIS failed with-1 and
produced no samples. Revision `60827f7` restored 1,024 B headroom and subsequently
ran IIS and UAC recording. That installed app was 563,440 B, with 479,000 B static RAM,
44,556 B linker heap and 4,544 B reviewed startup reserve. Its emitted diagnostic
closure plus SDK margin fits the 2,560 B USB stack. ADC input and applied gain
respond, and physical OPL music volume control is confirmed. Remaining noise
and physical output timing acceptance remain open.

## Output and volume

IIS uses PC0-PC2 clocks and PC6 channel-3 data, configured for 44.1 kHz stereo, signed 24-bit
samples in 32-bit words. One 1,024-byte ping-pong DMA buffer supplies 64 stereo
frames per callback. There is no new task, PCM queue or WAD cache. STOP/fatal
recovery quiesces the interrupt before closing the channel and freeing DMA.
Current output completes the mix and master envelope in a private 512 B block
before copying it into the DMA half and synchronizing.

The physical volume knob uses ADC4 on PB6 through the existing FM-1 volume
driver. It controls the master mix of music and effects. In the current source,
the existing CPU0 one-millisecond key timer schedules bounded ADC work every
two milliseconds under a separate volume lock. It publishes a volatile Q7 gain
target for the CPU1 audio callback; the callback no longer polls the ADC.
Startup stays muted until a valid knob reading.
Current startup opens IIS and configures its rate before ADC and ALINK, then
starts the timer before LCD/scanner setup. The timer continues polling during
LCD initialization and is reused once scanner setup completes. Previously
primed ADC state still survives the idempotent sound initialization API.
The newer NES UAC implementation polls volume from CPU0 DAC IRQ cadence;
Doom retains independent CPU0 timer polling and CPU1 audio rendering.
ADC ownership conflicts or conversion errors mute output and remain visible in
USB diagnostics. The original engine volume defaults still apply internally;
the reduced image menu does not expose the original sound sliders.

Doom and USB tasks use the SDK's `#C0` binding. ALINK has one owner on CPU1 at
interrupt priority 3, with its CPU0 route masked; teardown unregisters CPU1.
USB scalar audio diagnostics do not take the mixer lock. These observational
snapshots can straddle callbacks and do not prove a coherent moment in time.

## Music and effects

- The native original E1M1 MUS score has 5,826 events and a 96-second loop. Its
  private compressed score occupies 10,975 B. Use an original IWAD: an audit
  found inverted pitch-wheel events in the earlier WHX conversion.
- DOS OPL mode uses the original DMX/Chocolate Doom 1.9 OPL2 register rules,
  nine hardware voices and the exact 15 required GENMIDI patches. The sparse
  patch bank is 563 B, including 540 original instrument bytes. The portable
  integer emu8950 core runs at 49,716 Hz and is interpolated to 44,100 Hz.
- The optional synth mode applies VCO oscillator colour, VCA envelopes and
  resonant VCF filtering to the melody. It keeps original OPL percussion with
  reduced drum gain. E4 switches synth/OPL mode once per press. The requested
  original DOS mode is the boot default; synth treatment remains switchable.
- Two streamed effect voices decode original PCM8 samples losslessly from
  Rice blocks. Pistol, pickup, `oof`, menu open and menu close occupy 12,613 B.
  `noway` and `oof` share identical original samples. Linked chaingun effects
  use the pistol; other unpackaged effects are skipped before stealing an
  active channel. This bank does not contain all Doom sound effects.
- Effect mixer gain is reduced exactly 6.02 dB from the earlier `fc03a11` build.
  The decoded sample bytes and timing remain unchanged.

Generated game data and binaries remain in ignored `build/`; they are not
included in the public source repository.

### PCM effect path audit (2026-10-06)

The installed source closure uses exact PCM8/Rice at 11,025 Hz for all six
effect entries. Decoding converts unsigned samples to `(sample-128)*256`,
with no normalization. Conversion to nominal 44,100 Hz holds each sample for exactly
four output frames; there is no interpolation or padded decoded tail. The
declared sample count clears `playing` at the end, and stopped voices contribute
exactly zero. The music-only capture's zero effect voices therefore exclude
background sound-effect playback as its sustained noise source.

Engine defaults are effects/music8, passed as64 to the modules. Effects use
`left=floor(volume*(254-separation)/254)` and
`right=floor(volume*separation/254)`. Default centered separation128 gives31/32.
The pre-master effect is decoded PCM16 times that channel gain, with
`FM1_DOOM_SOUND_EFFECT_PCM24_GAIN=1`: relative to PCM16 expanded to PCM24, the
gain is31/256 or32/256 (-18.34/-18.06 dB), half the initial gain2 build.
The default centered pistol peaks at -18.34/-18.06 dBFS and lasts0.510567s,
before the master envelope.

OPL volume64 follows original DMX channel/operator attenuation rules, then its
sample is multiplied by4 and bounded to -8192..8191 before PCM24 expansion.
The music and up to two effects are summed, clipped to signed24, and both pass
through the same physical master gain/128. At default effect volume64, even
two fullscale effects panned to one channel plus bounded music cannot exceed
0.75 of fullscale. No signedness, rate, stale-playback or master-bypass defect
was identified. The simple hold can sound rough while an effect plays; it
does not generate effects when voices are stopped.

Percussion is separate from those PCM effects. E1M1 MUS channel15 maps through
MIDI channel9 to original GENMIDI patches, with 702 percussion note-ons using
12 OPL patches. Synth oscillators skip channel15; synth mode retains extracted
OPL drums at gain2, versus gain4 inside the normal OPL mix. Drums still obey
music/operator attenuation and the shared knob. Zero `sfx_voices` excludes
background effect playback but does not exclude OPL drums as the noisy music
component. Use the installed monitor build's DRUMS/MELODY comparison to test that
distinction; the percussion path is not sampled PCM playback.

## Generate private banks

Supply a lawful original shareware IWAD containing D_E1M1 and GENMIDI:

```powershell
python tools/convert_shareware_wad.py C:\path\to\doom1.wad
```

The tool creates private output under ignored `build/shareware-e1m1/`. When
linking that output, pass its generated `sound-bank/fm1_doom_sound_bank.c`,
`music-original/music_score.c` and `opl-bank/genmidi_bank.c` paths to the target
builder. The individual bank scripts and older WHX/IMA effect generator remain
available for custom workflows. Native PCM blocks have an exact sample count
and no padded playback tail.

The OPL driver is derived from GPL Chocolate Doom via the pinned
[rp2040-doom](https://github.com/kilograham/rp2040-doom) source. emu8950 retains
its MIT notices; [vendor provenance](vendor/emu8950/README.md) records the pin
and local adaptations. Original instrument data is supplied locally.

## Verification

The original DOS path was checked against an independent, unchanged pinned
Chocolate Doom driver reading the original MUS lump directly, with the
unchanged upstream emulator core. It matched 37,784 ordered register writes
and 4,233,600 output frames across the complete loop and runtime volume changes.
This checks software synthesis; it is not an analogue recording of a DOS card.
Lossless effects are checked byte for byte against original PCM and at their
exact playback length. Knob tests cover ADC registers, ownership, timeout,
raw endpoints, deadband and mute behaviour. An integration test exercises the
actual ADC driver through the output envelope at full, half, zero and restored
gain, and checks conversion timeout muting. It also primes the real driver
before sound initialization and verifies that raw/accepted/target/sample state
and ADC/GPIO registers survive without additional hardware writes. Failed
ADC startup/conversion cannot be silently retried; explicit shutdown permits
a new lifecycle. New event-based integration regressions verify IIS open/rate
before the first ADC access and ALINK enable afterward; IIS open/rate failures
perform no ADC accesses. All 12 native contracts pass for the `909c0188...`
image. Its installed physical sweep now spans ADC0..1023 and target/gain0..127
without ADC errors; the user confirms OPL music loudness follows the knob.
The earlier installed `8d7fc70d...` image also passes all 12 native contracts and exact
reference PCM, and its actual UAC recording is documented above.
The earlier `998677b7...` image was
flashed/readback-verified but did not resolve the user's knob report.
Physical output timing and remaining-noise acceptance remain open; UAC recording
is established for both `60827f7` and `8d7fc70d...`.

An unnormalized 16-second comparison at 44.1 kHz, music/effect volume 64 and
centred effects has zero clipped samples or music errors in both modes. Mixed
peak is -12.79 dBFS in OPL mode and -16.52 dBFS in synth mode. Synth drums are
exactly half original OPL drum amplitude sample for sample, and effects are
6.02 dB below the frozen initial build. Native x64 64-frame mixer p99 timings
are 48.75/63.60 microseconds; these do not establish PI32 callback timing.
The master knob and startup envelope are bypassed in that steady-state
comparison. OPL's original software-core DC offset is retained for fidelity;
synth mode's measured DC is -18.2 PCM16 units.

### Full-track host PCM inspection (2026-10-07)

The current repository sound/music/OPL/core and private banks were compiled
directly for an unnormalized 97-second, 44,100 Hz stereo PCM16 render: OPL mode 0,
monitor FULL, default music/effects 64, with both effect voices inactive. It
covers the full 96-second E1M1 score plus one second after looping. The real
board startup/master implementation also produced a separate master-127 WAV.
Source hashes were checked unchanged after rendering.

Premaster minimum/maximum is -7516/2976, peak -12.7893 dBFS, RMS -29.0226 dBFS,
DC -757.4211 PCM16 units and AC RMS 878.1005. There are zero quarter-limiter
endpoint hits at 8191/-8192, zero strictly clipped samples and zero fullscale
PCM16 endpoints. Pre-limiter OPL*4 already fits that range. The real master-127
render, including startup mute/ramp, peaks at -12.8577 dBFS.

All 4,233,600 first-loop frames and 37,778 ordered register writes at constant
volume 64 match the independently compiled unchanged original driver/core.
The 37,784-write contract above includes runtime volume 127/64 changes.
Music reports one loop and zero errors. The largest peak at 15.2731066 seconds
is 1.678 ms after original percussion note 57; the largest adjacent step,
-3740 at 87.5525397 seconds, also matches the reference and is not a 64-frame
boundary. No added clipping, arithmetic overflow or sample discontinuity was
identified in the compared music. Original FM transients and core DC remain.

Evidence is in ignored `build/host-bgm-pcm-check/`: the two WAVs,
`pcm-analysis.json`, `source-and-output-receipt.json`, `REPORT.md` and
`e1m1-pcm-inspection.png`. Offline synchronous rendering has no hardware DMA
deadline or USB consumer; it cannot rule out device underruns, clock/scheduling
problems, analog distortion or amplifier noise. It does not establish the
physical noise source or cold-boot USB recovery.

## USB diagnostics

`DOOM AUDIO` reports readiness, errors, output frames, effect starts, music
progress/loops/steals, `synth_mode` (0 OPL, 1 synth), `volume_raw`, `volume_gain`,
`volume_valid`, `volume_errors`, `volume_samples`, `volume_target`, callback
timing and minimum unused USB stack
in 32-bit words. The current candidate also reports `heap_free` through the
SDK's implemented allocator query, only on an explicit `DOOM AUDIO` request
and outside the audio lock. This is aggregate remaining allocator space,
including metadata/uncommitted arena space; it is neither the largest free
block nor minimum-ever free heap. `max_irq_us` uses a 500-microsecond clock;
actual callback time
can be up to 500 microseconds greater. The approximately 1,451-microsecond DMA
deadline must be checked on the device. This field is not an underrun counter.

The writer-compatible `DOOM STATUS` response is unchanged. `DOOM GAME` reports
position, skill, health/ammo, kills, sector and map geometry counts.
`DOOM FRAME BEGIN`, `DOOM FRAMEINFO`, `DOOM FRAME READ <offset>` and
`DOOM FRAME END` capture the existing 160x100 indexed frame and RGB565 palette.
The game pauses briefly between ticks; audio and key scanning continue.
Disconnect, STOP/error or a 15-second limit resumes gameplay automatically.

```powershell
python tools/capture_usb_frame.py --port COM6 --output build/usb-frame/screen.png
```

USB includes CDC and capture-only UAC1 at 44,100 Hz, stereo PCM16. USB audio playback
into the FM-1 is not implemented by this Doom port. The newer
[NES UAC profile](https://github.com/Keitark/fm1-nes/blob/9fa8d2352a9964241e3edbf16f9d2d22e8552623/firmware/usb-audio/profile.c)
supports playback and capture at 48,000 Hz, stereo PCM16; its 2026-10-03 deployment
verified enumeration of both Windows audio endpoints. That newer NES build is
separate from the older CDC-only image used for the ADC A/B test.

## Installed milestone evidence

The earlier audio app was flashed and readback verified on 2026-10-06, and the
user heard music and effects. Its 105-second Detailed-mode capture had zero
reported engine/LCD/key/audio/music errors and successful looping. Minimum
unused USB stack was 1,512 B; measured callback maximum was 500 microseconds,
meaning less than 1,000 microseconds with quantization. The gameplay counter
advanced about 10.9/s. Those figures describe the installed simplified synth,
not the new OPL/synth/knob revision. See [deployment](DEPLOYMENT.md).
