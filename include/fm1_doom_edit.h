#ifndef FM1_DOOM_EDIT_H
#define FM1_DOOM_EDIT_H

#include "fm1_doom_music.h"

/* NES encoder IDs: SELECT, ALGORITHM, KNOB1..4, PRESETS. */
typedef struct {
    fm1_doom_music_edit value;
    int32_t previous[7];
    int32_t algorithm_edges, preset_edges;
} fm1_doom_edit;

void fm1_doom_edit_init(fm1_doom_edit *edit);
/* Preserve pending movement; consume at most eight contact edges per input.
 * Algorithm/Presets use four edges per click; knobs use one edge per step. */
int fm1_doom_edit_update(fm1_doom_edit *edit, const int32_t counts[7]);
uint32_t fm1_doom_edit_pack(const fm1_doom_music_edit *value);

#endif
