#include "fm1_doom_edit.h"
#include "fm1_nes_fx.h"
#include <string.h>

static int edges(int32_t count, int32_t *previous)
{
    int64_t delta = (int64_t)count - *previous;
    if (delta > 8) delta = 8;
    if (delta < -8) delta = -8;
    *previous += (int32_t)delta;
    return (int)delta;
}

static int clicks(int32_t count, int32_t *previous, int32_t *remainder)
{
    int delta = edges(count, previous) + (int)*remainder;
    *remainder = delta % 4;
    return delta / 4;
}

void fm1_doom_edit_init(fm1_doom_edit *edit)
{
    static const fm1_doom_music_edit defaults = FM1_DOOM_MUSIC_EDIT_DEFAULT;
    static const fm1_doom_music_edit nes_defaults = FM1_DOOM_NES_FX_EDIT_DEFAULT;
    memset(edit, 0, sizeof(*edit));
    edit->value = defaults;
    edit->inactive = nes_defaults;
}

static unsigned clamp_choice(int value)
{
    return value < 0 ? 0u : value > 3 ? 3u : (unsigned)value;
}

static void nes_preset(fm1_doom_music_edit *edit, unsigned preset)
{
    fm1_fx_controls controls;
    fm1_fx_controls_reset(&controls);
    fm1_fx_control_edges(&controls, 6, (int32_t)preset * 4);
    edit->preset = controls.preset;
    edit->algorithm = controls.mode;
    edit->knob[0] = controls.cutoff;
    edit->knob[1] = controls.resonance;
    edit->knob[2] = controls.rate;
    edit->knob[3] = controls.depth;
}

int fm1_doom_edit_update(fm1_doom_edit *edit, const int32_t counts[7])
{
    fm1_doom_music_edit old = edit->value;
    unsigned old_bank = edit->bank;
    unsigned i;
    int delta;
    delta = clicks(counts[0], &edit->previous[0], &edit->select_edges);
    if (delta % 2) {
        edit->value = edit->inactive;
        edit->inactive = old;
        edit->bank ^= 1u;
        edit->algorithm_edges = edit->preset_edges = 0;
    }
    delta = clicks(counts[1], &edit->previous[1], &edit->algorithm_edges);
    edit->value.algorithm = (uint8_t)(edit->bank ? clamp_choice(edit->value.algorithm + delta)
                                               : (edit->value.algorithm + delta + 4) & 3);
    delta = clicks(counts[6], &edit->previous[6], &edit->preset_edges);
    if (edit->bank) {
        unsigned preset = clamp_choice(edit->value.preset + delta);
        if (preset != edit->value.preset) {
            nes_preset(&edit->value, preset);
            edit->algorithm_edges = 0;
        }
    } else edit->value.preset = (uint8_t)((edit->value.preset + delta + 4) & 3);
    for (i = 0; i < 4; ++i) {
        int value = edit->value.knob[i] + edges(counts[i + 2], &edit->previous[i + 2]);
        edit->value.knob[i] = (uint8_t)(value < 0 ? 0 : value > 127 ? 127 : value);
    }
    return old_bank != edit->bank || memcmp(&old, &edit->value, sizeof(old)) != 0;
}

uint32_t fm1_doom_edit_pack(const fm1_doom_music_edit *value)
{
    return (uint32_t)(value->knob[0] & 127u)
         | (uint32_t)(value->knob[1] & 127u) << 7
         | (uint32_t)(value->knob[2] & 127u) << 14
         | (uint32_t)(value->knob[3] & 127u) << 21
         | (uint32_t)(value->preset & 3u) << 28
         | (uint32_t)(value->algorithm & 3u) << 30;
}
