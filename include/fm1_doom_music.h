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
 * VCO/VCF/VCA treatment with original OPL drums. Mode1 is the default. */
void fm1_doom_music_set_synth_mode(unsigned mode);
unsigned fm1_doom_music_get_synth_mode(void);
void fm1_doom_music_toggle_synth_mode(void);
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
