# FM-1 Doom audio

The target uses the same IIS route as the working FM-1 NES build: PC0–PC2
clocks, PC6 channel-3 data, 44.1 kHz stereo and signed 24-bit samples in
32-bit words. The SDK allocates one 1,024-byte ping-pong DMA buffer; each
interrupt fills 64 stereo frames. Audio adds no task, PCM queue or WAD cache.
The callback reads only immutable XIP banks and bounded synthesizer state.
STOP and fatal-error recovery mask and quiesce the audio interrupt before
closing the channel and freeing DMA. Initialization failure leaves Doom muted.

## Included in the local build

- Two effect voices: original pistol, pickup and `oof` samples. `noway` uses
  `oof`; linked chaingun effects resolve to the pistol. Unpackaged effects are
  skipped before they can steal an active effect channel.
- The original E1M1 score, with 5,829 events and a 96-second loop. Conversion
  was checked against the upstream MUSX decoder event by event.
- Eight synthesizer voices with simplified guitar, bass, piano and percussion
  timbres. Notes, timing, velocity, volume, pan and pitch events are preserved;
  source polyphony peaks at ten, so voice stealing can alter dense chords.
- The existing digital startup mute and gain ramp. The original Doom sound
  menu controls effect and music volumes.

The tested private banks occupy 6,064 bytes for effects and 10,722 bytes for
music. Native IWAD versions may generate different sizes; the application
slot and startup-memory checks still apply. The generator's final IMA block
can add up to seven padded source samples, less than 0.9 ms.

## Generate banks locally

Supply your own IWAD. Generated game data and binaries stay under ignored
`build/`; they are not distributed with the source repository.

```powershell
python tools/make_sound_bank.py --wad C:\path\to\doom1.wad --output-dir build\sound-bank
python tools/make_music_score.py C:\path\to\doom1.wad build\music-bank
```

The bank tools also accept a locally converted WHX input. For effects use
`--whx` in place of `--wad`; for music pass that file as the positional input.
Music accepts a standalone original MUS lump too. WHX/MUSX format references
are the BSD-licensed [rp2040-doom](https://github.com/kilograham/rp2040-doom)
converter and decoder. The native-WAD path requires no WHX file.

## Diagnostics and limits

`DOOM AUDIO` reports IIS readiness, errors, interrupts, output frames, effect
starts, music events/loops/voice steals, and the USB task's minimum unused
stack in 32-bit words. `max_irq_us` measures the synchronous IIS callback using
the SDK's hardware-interpolated 500 µs clock. Actual callback duration can be
up to 500 µs above the reported maximum. The DMA deadline is approximately
1,451 µs; this measurement is not an underrun counter.

The original nine-field `DOOM STATUS` response remains compatible with the
protected writer. Audio acceptance also requires listening on the physical
FM-1 and checking controls while music and effects play.

Target string formatting supports integer and string conversions. Floating
conversions consume their arguments and emit `<float>`; the target has no
floating config input and retains its compiled defaults. Host builds retain
the original engine's formatting and configuration behavior.

## Device check, 2026-10-06

The audio app is flashed and complete-readback verified; the user hears music
and effects. During a 105-second Detailed-mode capture, audio remained ready,
music completed another loop, and all reported engine/LCD/key/audio/music
errors stayed zero. USB stack headroom was 1,512 B; the maximum measured audio
callback was 500 µs, indicating less than 1,000 µs with timer quantization.
Detailed mode retained about 10.9 frame-counter increments/s. These results
establish output/progress; the user reports sine-like timbres, so instrument
quality and music/effect balance are being checked. See [deployment](DEPLOYMENT.md).
