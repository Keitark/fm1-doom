# FM-1 Doom deployment, 2026-10-05–08

## Current result

The laptop's newer NES FX image completed sector/full-image verification but
failed startup. The laptop subsequently reported a user-authorized UBOOT
restore to the working NES image, with verified sectors/full readback and
successful serial boot. The failed Doom session is retained; physical NES
screen confirmation remains pending. No offline correction clears its state. See the
[allocator-capacity audit](ALLOCATOR_CAPACITY_AUDIT.md) for current evidence and
the freshly built private replacement candidate. It is unflashed and has no
hardware boot acceptance.

The earlier live editor from source `626d2612` ran: 565,744 B, app SHA-256
`70553874...`. The build removes the 8×8 presentation mode and adds synth
presets/algorithm and VCO/VCF/VCA/reverb editing. Its recovered 139-sector update
and complete 1 MiB readback match `a4c3fc49...`; one reset reached stage4/fault0
with advancing game/audio telemetry and zero LCD/key errors. Physical controls,
speaker output, streaming and timing acceptance remain pending. See the
[current deployment record](#live-editor-deployment-2026-10-07) and
[artifact gates](TARGET_CANDIDATE.md).

## Live-editor deployment, 2026-10-07

| Item | Value |
| --- | --- |
| Built source revision | `626d261208c6a1b9ae12783da524ea50c173b24d` |
| Plain `app.bin` | 565,744 B |
| App SHA-256 | `70553874c1d8dd2e89e1448d27fbf89b1d5476b8573f3a9cb1e89fa46cfd3d35` |
| ELF SHA-256 | `56b7036a6d5f175e5dd519dbdf1243e20b96444170ecc45d14a5732a0f33f126` |
| Verified full-image SHA-256 | `a4c3fc499f9a3bcf017d33bc48a7d2555e6eac04d52dde5863f3313b958c48a1` |
| App slot / remaining | 584,956 B / 19,212 B |
| Static RAM / linker heap | 479,160 B / 44,396 B |
| Reviewed startup reserve | 4,384 B; required 4,096 B |
| Shared writer source | `4363d8d`, companion FM-1 board project |

The first transfer lost its UBOOT disk connection after verifying 136 of the
139 planned sectors. The next sector was `0x8d000`; the directory sector
`0x4000` had not been committed. The failed session and original baseline,
candidate and sector scope were retained. The user reconnected the device on
a different USB connection and authorized proceeding.

Explicit `recover_flash` in a new protected snapshot first read all 1 MiB of
current flash. SHA-256
`0cd85354ca3461f6ca2f16f3b8ac6d8e84d2c4df3dd56563acd3c3307151d8d8`
was recorded. The recovery guard verified the 136 candidate sectors, untouched
baseline sectors and protected boot/config bytes; only the next uncertain
sector could differ. The changed target
identity was accepted only after this proof. The writer then rewrote the
original 139-sector scope, directory `0x4000` last. All 139 sector readbacks and
the final complete 1 MiB read matched the prepared image. The protected result
is `written_and_readback_verified`, with one complete final readback.
Ordinary retry still requires the original device identity. No protected latch
was cleared manually. The complete current read was used for validation,
retaining the original baseline and failure receipts.

One reset succeeded. COM4 `HELLO` returned `DOOM-FM1/1`; observation reached
stage4/fault0, frames128→161 and LCD/key errors0. A later read-only capture
showed gameplay frames624→757, audio frames2,578,432→2,977,216 and music
ticks8,065→9,331, with IIS ready1/error0 and music errors0. `GAME coarse=0`
confirms detailed presentation. All three editor samples reported
preset0/algorithm0/vco16/vcf72/vca32/reverb0/synth_mode0, confirming Original
OPL and default control state. Volume was valid1/errors0, raw259..260 and
target gain32; the physical knob was not swept during this capture.

A further 30.031-second read-only capture yielded 58 complete status lines,
all stage4/fault0 with LCD/key errors0, frames1,602→1,911. The final partial
line was excluded. Editor values stayed at their defaults; this is sustained
telemetry, not a physical encoder-response or distinct-frame-rate test.

Windows exposes healthy composite/CDC COM4 and FM1 MDX USB Audio devices. UAC
reported armed1/active0; no recording was made. Enumeration and advancing
audio counters do not establish audible speaker quality or USB streaming.
Minimum unused USB stack was 270 words, observed heap was 3,508 B and maximum
reported audio IRQ duration was 2,000 µs. Physical presets/algorithm/knobs,
speaker music/effects/master-volume acceptance, sustained DSP deadlines,
whole-level traversal and 30 completed distinct gameplay frames/s remain open.

Validation before recovery included 44 new recovery tests and 12 existing
retry/log tests, alongside the 13 Doom native contracts and target build gates.
The new recovery tests exercise complete-read shape/protected-byte rejection,
identity changes, retained failure latches and final readback failure.

Private receipts remain outside the public repository:

- Protected session: `FM1FlashSession-1e796214fbd742999c7eb22e214b5dd3`.
- Recovery proof/write/full readback: `runs/0910558fe3a2414784806c5041b717ed`.
- Single reset: `runs/f6e35792db8c4421b167df3735a19187`.
- Serial observation: `runs/a40794c7512442cdaca86a0b173834ba`.
- Original failed session: `FM1FlashSession-391bc34ea6334e74ae5ecd2b9e359d38`,
  `runs/86e5bf7d96aa49beaab5718ef64d0e70`.
- Read-only bench capture: ignored `build/bench-live-editor-20261007.json`.
- Sustained status capture: ignored `build/bench-live-editor-controls-20261007.json`.

## Historical cable and startup checkpoint

The user identified the connected programmer cable as the cause on 2026-10-07.
Switching to a normal USB data cable following the normal-boot check restored
the connection; the existing firmware exposes
healthy COM5 and a UAC recording endpoint. HELLO identifies DOOM-FM1/1;
stage4/fault0, frames1375->1409 and LCD/key errors0. No additional firmware
was flashed. The earlier missing-USB observations do not establish a Doom
cold-startup defect. See [USB_COLD_BOOT_AUDIT.md](USB_COLD_BOOT_AUDIT.md).

The previous application `3cf7f681...` (563,536 B) changes startup to create the peripheral
worker first, following public NES, and explicitly waits for IIS/ADC
initialization before USB attachment. Early game/audio failures still release
the USB worker for recovery. The causal role of that startup-order change
remains unconfirmed. Native contracts (12/12) and the
production USB task waiting/cancellation regression pass. Linker heap remains
44,556 B and reviewed startup reserve 4,544 B. The user accepts PCM and master
volume. Its startup-order change has not been established as a necessary fix.

At the earlier cable-dependent checkpoint, the 138-sector update passed complete readback, image SHA-256
`af92a30b658ee2f878c9b410ee53bf50391af9e3f3d86470500ada01de12f1f9`.
UBOOT reset exposes COM4 and the UAC recording endpoint. Serial observation
reaches stage4/fault0, frames123->157, LCD/key errors0. The user's subsequent
cold power cycle FAILED: Windows again exposes neither FM-1 serial nor UAC.
Startup ordering did not resolve that observation. The later cable finding
above supersedes this investigation. The live-editor changes were offline at
that checkpoint and have since been installed as recorded above.

The previous `ce8176d8...` application was installed with a matching complete
flash readback and successful warm serial boot. Audio now renders and applies
master volume in private memory before publishing a completed DMA block; the
user reports much improved, almost-perfect speaker playback. Cold-boot USB
still disappears, and remaining sound/timing acceptance is open. The installed
F4 startup screen makes USB register/status diagnostics available on the LCD.

The original engine reaches running stage 4 on the FM-1. The user confirms
that Doom gameplay is visible. Serial diagnostics report zero engine, LCD and
key-scanner errors in the revised build. The previous build's movement/menu/fire
worked after a power cycle, then keys stopped with `key_error=-3` while frames
continued. That scanner clock/recovery failure is corrected; sustained physical
   input and the new presentation toggle were confirmed working by the user.
The earlier `fc03a11` audio build was installed and full-readback verified. The user heard
music and sound effects. A 105-second Detailed-mode capture reports zero
engine/LCD/key/audio/music errors, successful music looping and roughly 10.9
frame-counter increments per second. Instrument quality and mix balance are
being checked. The requested 30 FPS target is not met; this is not a
distinct-frame benchmark or a full E1M1 traversal.

The historical `3318678` OPL/synth/knob build was installed and readback-verified,
but the user reports no audio and restricted movement near spawn after cold
boot. COM6 enumerates but requests time out. A compiled USB task stack overflow
was confirmed. The flashed `58b9ac4` repair restored USB stage 4 and frame
capture, but audio reported an IIS initialization failure (`ready=0`,
`error=-1`, `irqs=0`, `volume_valid=0`). The installed `03492fc` allocation fix
returns 1 KiB to the runtime heap and adds an explicit `heap_free` query.
Its full flash readback matched, but HELLO failed after one reset while COM10
enumerated. Later CPU/interrupt and door corrections restored serial boot and
frame capture; game snapshots show exploration beyond the corrected first
door. Full-map and physical door acceptance remain open.

On 2026-10-06, `60827f7` passed full readback and serial boot on COM4. IIS was
ready with 4,244 B live heap, and native 44.1 kHz stereo PCM16 USB capture was
recorded for 19.779 seconds. ADC stayed zero during a confirmed knob sweep,
and the producer delivered only about 33,454 frames/s. A controlled retained
NES reference then responded to the physical knob with ADC0..1023/errors0.
The next early-ADC Doom image (`f328adbc...`) booted but read nearly constant
221..222; the user confirmed it still failed. Image `ac0af0b6...` then passed
full readback and boot: IIS-first/ADC-before-LCD startup with early polling
restored ADC0..1023 and gain0..127, and the user confirms music volume control.
OPL production improved to about42,930 frames/s; the low-volume noise and
full-rate deadline remain open. The silent-operator image `50d6ee10...` passed
full readback and boot and recorded19.997 seconds of actual USB music with no
active PCM effects. Production measured43,086.91 frames/s; paused rendering
measured43,204.38, leaving the actual clock versus missed-IRQ cause unverified.
The user reports absent USB after a normal boot while Doom remains visible.
The newest compiled monitor build is described in
[TARGET_CANDIDATE.md](TARGET_CANDIDATE.md); its physical results are recorded
below when available.
See [PORT_STATUS.md](PORT_STATUS.md) for the current bench result and remaining
volume, timing and sound-quality gates.

## Graphics/input milestone artifacts

| Item | Value |
| --- | --- |
| Plain UBOOT `app.bin` | 569,840 B |
| App SHA-256 | `0463c496f4bf44a12af49ef0ab653808bffd8bc85ceda6ea7d73f036f4f4ef76` |
| Verified full-image SHA-256 | `9e51d686f98c3b735db3aa70f634727f1e9e216d1f55bffccebdc21423896f1c` |
| Application flash offset | `0x4120` |
| ELF/XIP entry | `0x02000120` |
| SDK revision | `e30b1ee375d1f2993fc23bf92c8b99006a6e5f9d` |
| Static RAM | 474,488 B |
| Linker heap | 49,068 B |
| Reviewed startup reserve | 4,448 B before other allocations/padding |
| Graphics archive | 181,995 B; original pistol/punch, 4×4 world/enemies |

The local build manifest freezes the source/header/configuration hashes. The
compiled archive remains private and is not included in the repository.

## Installation and observation

1. First image: 121 changed sectors and complete 1 MiB readback matched; user
   reported blank LCD after reset and cold boot.
2. USB diagnostic image: 124 changed sectors and complete readback matched.
   Reset and CDC boot succeeded. Doom reported stage 255, fault -30, frame zero:
   `Unknown configuration variable: 'joystick_physical_button'`.
3. Actual SDK formatter IR omits `%i` and consumes no argument for it. Generated
   printf conversions now use equivalent `%d`, including all three indexed
   configuration binders and fatal integer messages. Scanf `%i` is unchanged.
4. Serial STOP/UBOOT returned the device to download mode after the fatal error.
5. Final correction changed only `0x6d000`, `0x6f000`, `0x70000`, `0x71000`,
   `0x72000`, and directory sector `0x4000` last. One complete 1 MiB readback
   matched; its decoded app reproduces the built bytes and valid payload CRC.
6. One reset succeeded. COM10 identified `DOOM-FM1/1`; frames advanced 133 to
   167 across fresh status polls, with zero faults. User then confirmed gameplay.
7. After a power cycle the user confirmed movement, menu and firing, followed
   by a permanent input freeze. COM6 showed stage 4, advancing frames and
   `key_error=-3`. SDK IR confirms `timer_get_ms()` advances in 10 ms steps,
   equal to the scanner's entire 10 ms watchdog. Rapid polls across one tick
   can falsely timeout, and three lifetime retries then disabled input.
8. The scanner now uses `jiffies_half_msec()*500`, with 50–1000 ms recovery
   backoff and a one-second healthy period before resetting retries.
   A separate host stress case exposed 16 KiB composite texture allocation
   failure; a bounded 135-byte column cache removes that allocation.
9. Original pistol/punch patches and finer enemy/world assets are installed.
   D♯4 toggles Smooth 8×8 and Detailed 160×93 gameplay, both filling 240×240.
   Original menus stay detailed. Weapon projection uses the original 320-wide
   art coordinates. Normal CTest 5/5, x86 CTest 4/4 and Python 30/30 pass;
   all 145 archive blocks decode with the bounded inflater. Hybrid assets
   pass 600 firing/moving host ticks at a 296 KiB zone; Detailed passes 120.
10. This revised image wrote 140 changed sectors, directory last. Full 1 MiB
    readback matched, one reset succeeded, and COM6 showed stage 4, frames
    114→148 with zero engine/LCD/key errors.
11. A 60-second live capture produced 61 status lines: frames 629→1292,
    every engine/LCD/key error zero. Detailed mode remained selected, scanner
    sequences and IRQ completions advanced, retry count stayed zero and no
    scanner failure was recorded. The approximate counter rate is 11.05/s;
    this is not a distinct-render-frame benchmark. The user confirmed stable
    movement/firing and both presentation modes working without a freeze.

Representative device status:

```text
DOOM STATUS stage=4 fault=0 frames=148 stopped=0 lcd_stage=4 lcd_error=0 key_error=0 sys_hz=360000000 lsb_hz=60000000 error=
```

Protected receipts/logs are in the local session
`C:\Program Files\FM1FlashSession-3ff488471a1f45859099b12c58b418dc`:

- Graphics/input deployment/readback: `runs/9c3f25636fef418e8250f22e60c86b69`.
- Graphics/input reset: `runs/127184c5570047c39c0333c7ee712541`.
- Graphics/input CDC observation: `runs/955a2b220b1c44578f0789995aab41f7`.
- Graphics/input serial UBOOT entry: `runs/b18950ffa4a641609e572ae626ba7a18`.
- Prior verified image: `runs/a0bcbb98b06f4c0694dd9a9c87eefefd`.

No raw image, game data, device key or binary is committed.
Shared support changes are in
[fm1-tracker PR #5](https://github.com/Keitark/fm1-tracker/pull/5), based on
the existing tracker branch. The Doom PR remains draft pending runtime
Doom-stack/heap, full-level behavior, audio quality and performance acceptance.

## Audio installation, 2026-10-06

| Item | Value |
| --- | --- |
| Plain UBOOT `app.bin` | 583,216 B; 1,740 B below the reviewed slot limit |
| App SHA-256 | `715f58b29fd2f0280ead27adf975aa5908b6832b5b6ebf92634ca7426a9927a5` |
| Verified full-image SHA-256 | `ce9544e7730372690c9912a83d17ae73d641406c9153d26ada64ce3a7eea6ca9` |
| Application flash offset / ELF entry | `0x4120` / `0x02000120` |
| Static RAM / linker heap | 475,176 B / 48,364 B |
| Reviewed startup reserve | 6,816 B before other allocations/padding, including IIS DMA |
| Graphics archive | 177,331 B; original pistol, 2×2 fist, 4×4 world/enemies |
| Effect / score banks | 6,064 B / 10,722 B; private XIP data |

The writer changed 143 sectors, `0x5000` through `0x92000`, then directory
sector `0x4000` last. Per-sector verification and one complete 1 MiB readback
matched. The normal boot/config/reserved/tail regions are preserved. One reset
completed and COM6 identified `DOOM-FM1/1`, stage 4, frames 149→182, with zero
engine/LCD/key faults.

A 105-second capture collected 209 STATUS and 104 AUDIO/TRACE records. Detailed
mode remained selected; frames advanced 1084→2220 over 104.078 seconds, about
10.915 counter increments/s. Audio interrupts advanced 69,160→140,162 and
output frames 4,426,240→8,970,368. Music loops advanced 1→2 with no score errors;
readiness stayed 1 and all reported engine/LCD/key/audio errors stayed zero.
Scanner counters advanced, retries and failures stayed zero. No keys were held
during this captured interval; sustained physical controls were established by
the preceding graphics/input milestone.

The USB task's minimum unused stack was 378 words, or 1,512 B. The maximum
reported callback duration was 500 µs, giving a bound below 1,000 µs with the
500 µs timer quantization. The DMA deadline is about 1,451 µs. This telemetry
does not count underruns or establish the actual minimum free heap/Doom stack.
The user confirms music and effects are audible and reports simple, sine-like
instrument timbres; mix and instrument quality are being checked separately.

Current protected receipts:

- Serial UBOOT entry: `runs/85161862727445b49889c6fa933c00eb`.
- Audio deployment/readback: `runs/9410445302504833b3bd7b79742075ff`.
- Audio reset: `runs/c40cb8c08c274f3a833f0207f8c6ad7b`.
- Audio CDC observation: `runs/2d59691e4abf42988ffb2c8fa9e50d64`.
- Local capture: ignored `build/target-candidate/audio-live.txt` and
  `audio-live-summary.json`.

The build passed 43 Python tests, four focused audio/protocol/formatter CTests,
target compile/link and all 411 frozen source/config/header hashes. The builder
manifest describes offline readiness; these separate receipts establish actual
installation and boot.

## OPL/synth/knob installation and USB regression

Revision `3318678` produced a 554,800 B app, SHA-256
`a908bde332159a94b3ab86f8eb769cb103e3e70fd135087cb319383763f71360`.
The writer verified all 136 changed sectors and one complete 1 MiB readback,
SHA-256 `ebbc6bcda76c51ccf84b32e3c528115c68c5c8d29ac7a40627eab1ab6146b95f`.
The directory sector was written last; boot/config/tail were preserved.
Protected receipt: `runs/67007d29eb024b52b33d9434d41e5fba` in the session above.
One reset (`runs/2d9bfcf6251140fb937cfa866290e27a`) completed. Observation failed
(`runs/94481b75111345a7aa26c06d791a209c`); a physical cold boot did not recover
USB or sound. No reset or write retry was issued.

The final ELF has a 3,192 B USB task frame. Periodic status formatting adds
956 B plus the integer formatter closure, reaching 4,320 B on a 4,096 B task
stack. This is a confirmed overflow; it does not independently establish the
cause of the silence or the user's navigation report.

The source repair prevents LTO from merging mutually exclusive reply buffers.
Its app is 554,768 B, SHA-256
`64194b06e5b372e0b671b690e04b7a90aad56a89fc59380ad055812f74a6e373`.
Task frame: 136 B; deepest diagnostic chain: 1,424 B. The builder now reads
actual PI32 register saves/local allocations from the final ELF and enforces
the diagnostic bound plus 1,024 B SDK margin within the unchanged task stack.
Separate audio interrupt stack usage is 384 B on a 4,096 B SSP. Runtime USB
high-water, audio IRQ timing, knob and map capture still require device checks.

The failed observation latch and its verified readback are preserved. A fresh
protected writer session used that readback as its baseline after Windows UAC
approval. Physical UBOOT entry allowed the USB repair installation below.

## USB repair installation and IIS diagnosis

Revision `58b9ac4` wrote 136 changed sectors, `0x5000` through `0x8b000`, then
directory sector `0x4000` last. Per-sector verification and one complete 1 MiB
readback matched SHA-256
`71a78229cec1355d89f95d0a361cfcc751f07571397c0acd28f95b1480d61e2d`.
One reset succeeded. COM10 identified `DOOM-FM1/1`, stage 4, frames 112→146,
with zero engine/LCD/key errors. Guarded serial STOP/UBOOT works again.

Explicit audio status showed `ready=0`, `error=-1`, `irqs=0`, `frames=0` and
`volume_valid=0`. IIS initialization fails before music, output callbacks or
volume conversion run. The USB stack's minimum unused telemetry was 667 words,
or 2,668 B, after successful physical frame capture. That capture showed the
original spawn room, including its right-hand opening; it does not reproduce
the user's reported stopping area.

Protected session:
`C:\Program Files\FM1FlashSession-576aa49beb9e4b4280a66275279ec22f`.

- Deployment/readback: `runs/b038030ecf7b4e36a81121540c41ff06`.
- Reset: `runs/500a7f68820f4184904bcba0a77c3d61`.
- Observation: `runs/0ab8a7fa074b4677871d3a152ea9c11a`.
- Guarded serial UBOOT: `runs/17e3a758a96544d5b19e941c669b34b8`.
- Local diagnostics: ignored `build/target-candidate/usb-repair-live/`.
- Local frame/palette capture: ignored `build/usb-frame/repair-screen.png`
  and `repair-screen-source.png`.

## Audio allocation installation and reported knob failure

Revision `03492fc` produces a 555,024 B app, SHA-256
`fb230df6e1d5bf6f13232551d26638a1caea01eb012a2a3b2a67c91e6cf8b246`.
ELF SHA-256 is
`b7df5e2aa2220091f801daa8177b418e642757ed5fdd291fac365beca171aa0d`.
Static RAM remains 477,912 B and linker heap 45,644 B. Reducing USB task
allocation from 4,096 to 3,072 B returns 1 KiB to that heap. Reviewed task
minimum is 37,464 B and startup reserve 5,120 B before other allocations.
The emitted diagnostic chain is 1,436 B; with 1,024 B SDK margin it leaves
612 B in the USB allocation. Doom zone, archive cache and graphics are unchanged.

Only explicit `DOOM AUDIO` requests query the SDK allocator outside the audio
lock. `heap_free` is aggregate remaining space including metadata/uncommitted
arena space; it is not largest-block or minimum-ever free heap evidence.
Python 71/71, the native USB protocol contract, target link and all 424 frozen
source/config/header hashes pass.

The protected writer verified 136 changed sectors and one complete 1 MiB
readback matching SHA-256
`d2ed5405c1e1c24e8fc75bb77004d59e77a1d25035681953370a1e40a205ae76`.
One reset completed. COM10 enumerates, but both the protected observation and
a bounded read-only HELLO check failed. The observation failure latch remains
intact; no reset or write retry was issued. A physical cold power cycle was
requested. The user subsequently confirms audible music, but the knob has no
effect and USB appears hung. Windows currently sees no FM-1 serial port;
fresh runtime diagnostics await reconnection. This establishes reported sound
output without accepting knob control, USB recovery or callback deadlines.

Receipts in the fresh session above:

- Deployment/readback: `runs/45e85ac3f5a84c0ea0e8e3e239c76be8`.
- Reset: `runs/b64ebe282c1d4c1da83fe4c2c7511e28`.
- Failed observation: `runs/b2fa6288b2d249b1904db8939ac422c0`.

## Historical CPU/volume candidate before installation

Doom and USB tasks are bound to CPU0 with the SDK's `#C0` notation. ALINK is
routed only to CPU1 at interrupt priority 3, with CPU0's route masked and
teardown unregistering CPU1. USB scalar audio diagnostics no longer take the
mixer lock; snapshots may straddle callbacks. The cause of the observed USB
loss has not been established by fresh device telemetry.

The existing CPU0 key timer polls PB6 ADC4 every two milliseconds under a
separate volume lock. A volatile Q7 gain target reaches the CPU1 audio callback;
ADC work no longer runs in that callback. `DOOM AUDIO` adds target gain and
completed-sample count. The integration contract exercises the actual ADC
driver and output envelope at full, half, zero and restored gain, plus timeout
muting. Python 72/72 and three native USB/sound/volume-integration contracts pass.
Scalar diagnostics also return with the mixer lock held. All 79 engine and
seven port sources were recompiled and the target link passes.

| Candidate item | Value |
| --- | --- |
| Plain `app.bin` | 555,120 B |
| App SHA-256 | `b0efff90e99d7f767c8e938f32d2906120f2fe07ff8604ec2b80c33a8315356e` |
| ELF SHA-256 | `cc757ada7c93c78570d77146784e49a692f8fe3d0f2b18d37e5f63de39ddffe5` |
| Static RAM / linker heap | 477,896 B / 45,644 B |
| Reviewed task minimum / startup reserve | 37,464 B / 5,120 B before other allocations/padding |
| USB task / diagnostic chain / SDK margin | 3,072 B / 1,460 B / 1,024 B; 588 B remaining |

No new device write has occurred. Reconnection, physical UBOOT entry and a
fresh protected writer session are pending. That session will use the verified
`d2ed…` readback receipt as its baseline; the old observation failure latch
remains intact. Installation and physical knob/USB acceptance remain open.

## Historical door-fix candidate before installation

Graphics pruning removed the original `AASTINKY` texture-zero entry and made
`BIGDOOR2` index zero. The renderer treats zero as no texture, so the door was
skipped while BSP clipping still treated it as a solid closed door. The stage
tool now retains that original entry and its `WALL00_3` patch closure. The
archive is regenerated from the original IWAD; an already-pruned archive
cannot restore the missing entry and patch.

Exactly one patch lump is added. All other logical patch pixels, map lumps and
resolved texture content remain unchanged; `BIGDOOR2` is now index 1. The
synthetic regression fails with the old pruner, and all 74 Python tests pass.
Host screenshots show an opaque closed door in both Smooth and Detailed modes;
one F5 edge changes its ceiling height from 0 to 68. These are host results,
not physical display or whole-level acceptance.

| Candidate item | Value |
| --- | --- |
| Private staged WAD / FMD1 archive | 553,048 B / 170,636 B |
| Archive SHA-256 | `197d7ccfc475893a6a5b581ea63c51003c524aaa4c38096de370329f2fd2fe7f` |
| Plain `app.bin` | 555,248 B; 29,708 B below the reviewed slot limit |
| App SHA-256 | `238d08700d99dd700e51ea7ef40d7d810f262b5722ab09268a94ad5f75eaee4e` |
| ELF SHA-256 | `25f1beabb4d843aad86bdc96797830c9ab89367cb3b8f2e770feee78c88b178b` |
| Static RAM / linker heap | 477,896 B / 45,644 B |
| Reviewed task minimum / startup reserve | 37,464 B / 5,120 B before other allocations/padding |
| USB task / diagnostic chain / SDK margin | 3,072 B / 1,460 B / 1,024 B; 588 B remaining |
| Frozen source/config/header hashes | 424; all match |

The CPU0 task binding, CPU1 ALINK owner and separate CPU0 volume polling from
the preceding candidate are retained. The installed `03492fc` image and its
verified `d2ed…` readback receipt remain unchanged. Neither candidate has been
flashed. Windows currently exposes no FM-1 serial/UBOOT device, and the fresh
protected writer's UAC approval remains pending. There is no new deployment,
reset or readback receipt. Physical door, knob and USB acceptance and the
requested 30 FPS remain open.

## Private audio buffer deployment, 2026-10-07

| Item | Value |
| --- | --- |
| Plain `app.bin` | 563,440 B |
| App SHA-256 | `ce8176d84946623efc296434973dbabc75c094edd3b7a8cb8ea29fbc39762ab3` |
| ELF SHA-256 | `a07105f20eef7596f407ecfb308475c64fba0e8af426ce6efbcab5b2121758df` |
| Verified full image | `2a432d0f469a21169a5cbe638fb2c5f95e9be493845eadd2eda0cd92951a6a1b` |
| Static RAM / linker heap | 479,000 B / 44,556 B |
| Reviewed startup reserve | 4,544 B, before other allocation padding |

The callback now renders into a private 512 B block, applies the master
envelope there, and publishes the finished block with `memcpy` and `csync`.
USB still records the pre-master mix. Regressions prove that the live DAC
buffer remains unchanged through all 64 sample-generation calls at low gain
and mute. All 12 native contracts pass; target source closure, slot, RAM,
startup budget and USB stack gates pass. Inlining `update_noise` preserves
the independent reference PCM and register sequence.

The existing protected writer changed 138 sectors, directory last, and the
complete 1 MiB readback matched. A single warm reset then produced COM4,
`DOOM-FM1/1`, stage 4/fault 0, frames 109→142 and zero LCD/key errors.
The user reports the speaker is much improved and almost perfect. Remaining
glitches are not yet accepted as resolved.

COM4 was absent when the subsequent UAC recording attempted to open it, so
that attempt produced no new device audio. Windows also exposed no FM-1
serial/UAC/UBOOT device. The user reports that cold-boot USB still disappears.
The new optional F4 LCD startup screen is installed; its physical register
photo is pending. This deployment does not establish a cold-boot USB fix or
sustained DMA deadline compliance.

Receipts in protected session `FM1FlashSession-391bc34ea6334e74ae5ecd2b9e359d38`:

- Write/full readback: `runs/9f0beb1c909140e2afe777bd4ad725b6`.
- Reset: `runs/55aea7d4083f444dba015383ddfdfdf7`.
- Warm serial observation: `runs/01aa99100ff7416d9170c898d9d7a012`.

The previous `6150df02...` monitor build also passed full readback and warm
boot. Its cheap PAUSE interval stopped both engine work and MUS/OPL generation
and delivered about 44.1 kHz, with no new capture underflows/overflows/silent
frames in that interval. That result disfavors the earlier fixed-clock
hypothesis; it does not isolate synthesis from other runtime load.
