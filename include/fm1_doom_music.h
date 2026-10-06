#ifndef FM1_DOOM_MUSIC_H
#define FM1_DOOM_MUSIC_H
#include <stddef.h>
#include <stdint.h>

/* Heapless DOS Doom OPL2 music with nine hardware voices at 44.1 kHz output.
 * All low-level calls
 * must share the sound backend's audio lock; sample functions never lock.
 * Game-data score bytes are provided separately from ignored build/music-bank. */
int fm1_doom_music_set_score(const uint8_t *score, size_t length);
int fm1_doom_music_start(int loop);
void fm1_doom_music_stop_locked(void);
void fm1_doom_music_pause(int paused);
void fm1_doom_music_set_volume(unsigned volume);
/* Caller holds the sound lock. Mode0: original DOS OPL2; mode1: melodic
 * VCO/VCF/VCA treatment with original OPL drums. Mode0 is the default. */
void fm1_doom_music_set_synth_mode(unsigned mode);
unsigned fm1_doom_music_get_synth_mode(void);
void fm1_doom_music_toggle_synth_mode(void);
typedef struct {
    uint8_t preset, algorithm, knob[4];
} fm1_doom_music_edit;
#define FM1_DOOM_MUSIC_EDIT_DEFAULT {0, 0, {16, 72, 32, 0}}
/* Caller holds the same sound lock used by rendering. Presets: Original,
 * Warm, Acid, Room. Selecting a different preset changes OPL/synth mode;
 * editing knobs alone preserves the legacy E4 mode toggle. Algorithms 0..3
 * choose classic, triangle, pulse and mixed band-pass synthesis. Knobs 0..3
 * control VCO detune, VCF cutoff, VCA attack/release and room wet amount.
 * Set/get controls never allocate or restart notes. */
void fm1_doom_music_set_edit_controls(const fm1_doom_music_edit *edit);
void fm1_doom_music_get_edit_controls(fm1_doom_music_edit *edit);
void fm1_doom_music_reset_edit_controls(void);
/* Derive and smooth controls once before each 64-frame mixer block. */
void fm1_doom_music_begin_block(void);
/* Optional caller-owned mono PCM16 room delay, at most 1024 samples (2 KiB).
 * NULL or fewer than 32 samples disables it. Binding clears the used buffer;
 * the caller retains its lifetime and holds the sound lock throughout. */
void fm1_doom_music_set_reverb_buffer(int16_t *buffer, size_t samples);
/* Temporary USB listening diagnostics. Caller holds the sound lock. PAUSE
 * freezes music generation without changing the engine's pause ownership. */
enum {
    FM1_DOOM_MUSIC_MONITOR_FULL,
    FM1_DOOM_MUSIC_MONITOR_MELODY,
    FM1_DOOM_MUSIC_MONITOR_DRUMS,
    FM1_DOOM_MUSIC_MONITOR_PAUSE
};
void fm1_doom_music_set_monitor(unsigned mode);
unsigned fm1_doom_music_get_monitor(void);
int fm1_doom_music_is_playing(void);
int32_t fm1_doom_music_sample(void);
void fm1_doom_music_sample_stereo(int16_t *left, int16_t *right);

typedef struct {
    uint32_t ticks, events, loops, voice_steals, errors;
    unsigned active_voices;
} fm1_doom_music_diagnostics;
void fm1_doom_music_get_diagnostics(fm1_doom_music_diagnostics *diagnostics);

#ifdef FM1_TARGET_PI32V2
#include "i_sound.h"
extern music_module_t fm1_music_module;
#endif
#endif
