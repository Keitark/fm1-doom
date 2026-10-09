# Portable OPL2 core

Source: https://github.com/kilograham/rp2040-doom, commit
`29a453c980918a03e40fc8b69b024e7a3bdb5dc2`, `opl/emu8950.{c,h}` and `slot_render.h`.
Original emulator: Mitsutaka Okazaki's emu8950 v1.1.0, MIT license; Graham
Sanderson's portable integer adaptations retain the same license notices.

FM-1 changes: fixed integer portable configuration; static constructor; compute
the key-scale-rate lookup with the exact original table-generation formula,
saving 512 bytes of RAM; immutable waveform masks live in XIP. Native synthesis
remains 49,716 Hz and is resampled by the audio port. ARM block rendering,
floating point conversion, timers, and ADPCM peripherals are unused.

The noise LFSR advances in exact eight-step GF(2) chunks and short-noise work
is skipped while rhythm mode is disabled. Its state remains identical if
rhythm is enabled later. Tests compare 65,536 arbitrary-seed rhythm-toggle
samples with the unchanged core, in addition to the full E1M1 PCM comparison.

Original source SHA-256:

`src/fm1_doom_opl.c` separately adapts the same pinned repository's
`src/i_oplmusic.c` under GPL-2.0-or-later (its original license notices are
retained in that file). Its frequency/volume tables and Doom 1.9 event handlers
are compared against unchanged source in the optional original OPL host test.
The emulator's key-scale-rate formula and static construction are also tested
against the unchanged emulator over the full original score.
- `emu8950.c`: `266e4fdee00c4f72c8b7839aa3398eab039a7082717969e5f7aba1ac4f317474`
- `emu8950.h`: `c609c52e4b60ca6f8b4a75da4d8e0d15089c2bc3169befc14680eef1e928e1a2`
- `slot_render.h`: `b10a966511aa7bee3d0f4189453ef0e137028f09bdc4f7b3857094ef14260985`
