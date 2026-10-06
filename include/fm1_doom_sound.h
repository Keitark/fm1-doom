#ifndef FM1_DOOM_SOUND_H
#define FM1_DOOM_SOUND_H

#include <stddef.h>
#include <stdint.h>
#include "i_sound.h"
#include "fm1_doom_volume.h"

#define FM1_DOOM_SOUND_VOICES 2u
#define FM1_DOOM_SOUND_OUTPUT_RATE 44100u
#ifndef FM1_DOOM_BOOT_MUTED
#define FM1_DOOM_BOOT_MUTED 0
#endif
/* Preserve exact decoded PCM; reduce effects 6.02 dB relative to the first
 * audio build. The physical master knob scales music and effects together. */
#define FM1_DOOM_SOUND_EFFECT_PCM24_GAIN 1

/* Private, immutable XIP assets; these indices are not WAD lump numbers. */
typedef struct {
    char name[9];
    const uint8_t *data;
    uint32_t bytes;
} fm1_doom_sound_entry;

extern const uint8_t fm1_doom_sound_bank[];
extern const fm1_doom_sound_entry fm1_doom_sound_entries[];
extern const uint32_t fm1_doom_sound_entry_count;
extern const uint32_t fm1_doom_sound_bank_bytes;

/* Streaming IMA or exact PCM8/Rice state. No decoded waveform or block-sized
 * scratch buffer. PCM8/Rice uses at most 15 source bits per next sample. */
typedef struct {
    const uint8_t *next, *end;
    uint32_t phase, step;
    int32_t predictor;
    uint32_t samples_left; /* Rice: includes the currently held PCM sample. */
    uint16_t block_left;
    uint8_t index, high, left, right, playing, encoding;
} fm1_doom_sound_voice;

int fm1_doom_sound_voice_start(fm1_doom_sound_voice *voice,
                              const uint8_t *data, size_t bytes);
void fm1_doom_sound_mix(fm1_doom_sound_voice voices[FM1_DOOM_SOUND_VOICES],
                       int32_t stereo[128]);

extern sound_module_t fm1_sound_module;
extern volatile int fm1_doom_sound_error;
extern volatile uint32_t fm1_doom_sound_irqs, fm1_doom_sound_frames;
extern volatile uint32_t fm1_doom_sound_started;
typedef struct {
    uint32_t ready, irq_count, output_frames, sfx_started, active_voices;
    int error;
    uint32_t max_irq_us; /* 500 us quantization; actual duration may be <500 us larger. */
    uint16_t volume_raw;
    uint8_t volume_gain, volume_valid;
    uint32_t volume_errors, volume_samples;
    uint8_t volume_target, speaker_muted;
    uint32_t synth_mode;
} fm1_doom_sound_diagnostics;
/* Bounded scalar diagnostics; no mixer lock. Values may straddle a callback.
 * IRQ counts do not measure missed DMA deadlines. */
void fm1_doom_sound_get_diagnostics(fm1_doom_sound_diagnostics *diagnostics);
/* Existing CPU0 input timer calls this once every 2 ms, independently of DMA.
 * It owns the ADC through a separate short lock, with no music rendering. */
void fm1_doom_sound_volume_tick(void);
/* CPU0 changes only the published control word under the ADC-side lock.
 * Muting ramps the speaker gain to zero; native USB capture remains active.
 * The getter is a best-effort scalar read, without the audio/render lock. */
void fm1_doom_sound_set_speaker_muted(unsigned muted);
unsigned fm1_doom_sound_speaker_is_muted(void);
/* CPU0 task-side, bounded read-only ADC/GPIO snapshot under volume_lock.
 * Gain is a best-effort CPU1 scalar; no audio/render lock is taken. */
void fm1_doom_sound_get_volume_hardware(fm1_doom_volume_hardware *hardware);
/* Return zero on success. Failure leaves audio silent and is nonfatal to Doom. */
int fm1_doom_sound_init(void);
int fm1_doom_sound_is_ready(void);
void fm1_doom_sound_shutdown(void);
/* Task-side mode switch serialized with the IRQ music renderer. */
void fm1_doom_sound_toggle_music_mode(void);
/* Task-side music controls use the same IRQ-safe lock as the DAC mixer.
 * IRQ music sample/stop helpers are called with this lock already held. */
unsigned fm1_doom_sound_lock(void);
void fm1_doom_sound_unlock(unsigned flags);

#endif
