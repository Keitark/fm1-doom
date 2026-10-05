# FM-1 Doom audio

The earlier `fc03a11` milestone played music and effects with simplified timbres.
The installed OPL/synth/knob revision `3318678` is readback-verified, but the user
reports silence after a cold boot and USB requests fail. A confirmed USB task
stack overflow is repaired in source; installation and knob diagnostics are
pending. The overflow is not yet established as the cause of the silence.

## Output and volume

IIS uses PC0-PC2 clocks and PC6 channel-3 data, 44.1 kHz stereo, signed 24-bit
samples in 32-bit words. One 1,024-byte ping-pong DMA buffer supplies 64 stereo
frames per callback. There is no new task, PCM queue or WAD cache. STOP/fatal
recovery quiesces the interrupt before closing the channel and freeing DMA.

The physical volume knob uses ADC4 on PB6 through the existing FM-1 volume
driver. It controls the master mix of music and effects. The callback schedules
bounded ADC work at an average two-millisecond cadence; it does not wait for a
conversion or allocate memory. Startup stays muted until a valid knob reading.
ADC ownership conflicts or conversion errors mute output and remain visible in
USB diagnostics. The original engine volume defaults still apply internally;
the reduced image menu does not expose the original sound sliders.

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
  synth treatment is enabled by default; DOS mode remains available.
- Two streamed effect voices decode original PCM8 samples losslessly from
  Rice blocks. Pistol, pickup, `oof`, menu open and menu close occupy 12,613 B.
  `noway` and `oof` share identical original samples. Linked chaingun effects
  use the pistol; other unpackaged effects are skipped before stealing an
  active channel. This bank does not contain all Doom sound effects.
- Effect mixer gain is reduced exactly 6.02 dB from the installed build.
  The decoded sample bytes and timing remain unchanged.

Generated game data and binaries remain in ignored `build/`; they are not
included in the public source repository.

## Generate private banks

Supply a lawful original shareware IWAD containing D_E1M1 and GENMIDI:

```powershell
python tools/make_sound_bank.py --wad C:\path\to\doom1.wad --menu --output-dir build\sound-bank
python tools/make_music_score.py C:\path\to\doom1.wad build\music-original
python tools/make_genmidi_bank.py C:\path\to\doom1.wad build\opl-bank
```

Use `--music-bank build/music-original/music_score.c` when linking the target.
The older WHX/IMA effect generator remains available for compatibility. Native
PCM blocks have an exact sample count and no padded playback tail.

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
raw endpoints, deadband and mute behaviour. Physical acceptance of this revision
is still pending.

An unnormalized 16-second comparison at 44.1 kHz, music/effect volume 64 and
centred effects has zero clipped samples or music errors in both modes. Mixed
peak is -12.79 dBFS in OPL mode and -16.52 dBFS in synth mode. Synth drums are
exactly half original OPL drum amplitude sample for sample, and effects are
6.02 dB below the frozen initial build. Native x64 64-frame mixer p99 timings
are 48.75/63.60 microseconds; these do not establish PI32 callback timing.
The master knob and startup envelope are bypassed in that steady-state
comparison. OPL's original software-core DC offset is retained for fidelity;
synth mode's measured DC is -18.2 PCM16 units.

## USB diagnostics

`DOOM AUDIO` reports readiness, errors, output frames, effect starts, music
progress/loops/steals, `synth_mode` (0 OPL, 1 synth), `volume_raw`, `volume_gain`,
`volume_valid`, `volume_errors`, callback timing and minimum unused USB stack
in 32-bit words. `max_irq_us` uses a 500-microsecond clock; actual callback time
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

USB is CDC only. USB Audio Class output is not implemented.

## Installed milestone evidence

The earlier audio app was flashed and readback verified on 2026-10-06, and the
user heard music and effects. Its 105-second Detailed-mode capture had zero
reported engine/LCD/key/audio/music errors and successful looping. Minimum
unused USB stack was 1,512 B; measured callback maximum was 500 microseconds,
meaning less than 1,000 microseconds with quantization. The gameplay counter
advanced about 10.9/s. Those figures describe the installed simplified synth,
not the new OPL/synth/knob revision. See [deployment](DEPLOYMENT.md).
