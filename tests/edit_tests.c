#include "fm1_doom_edit.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static const unsigned selectors[] = {1, 6};

static unsigned selected(const fm1_doom_edit *edit, unsigned id)
{
    return id == 1 ? edit->value.algorithm : edit->value.preset;
}

static int32_t remainder(const fm1_doom_edit *edit, unsigned id)
{
    return id == 1 ? edit->algorithm_edges : edit->preset_edges;
}

static int test_defaults_reset_all_encoder_state(void)
{
    static const uint8_t knobs[] = {16, 72, 32, 0};
    fm1_doom_edit edit;
    int32_t counts[7] = {0};
    unsigned i;
    memset(&edit, 0xa5, sizeof(edit));
    fm1_doom_edit_init(&edit);
    CHECK(edit.value.preset == 0 && edit.value.algorithm == 0);
    CHECK(!memcmp(edit.value.knob, knobs, sizeof(knobs)));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH);
    CHECK(edit.algorithm_edges == 0 && edit.preset_edges == 0 && edit.select_edges == 0);
    for (i = 0; i < 7; ++i) CHECK(edit.previous[i] == 0);
    CHECK(!fm1_doom_edit_update(&edit, counts));
    return 0;
}

static int test_select_requires_four_net_edges_and_wraps_both_banks(void)
{
    fm1_doom_edit edit;
    static const fm1_doom_music_edit synth = FM1_DOOM_MUSIC_EDIT_DEFAULT;
    static const fm1_doom_music_edit nes = FM1_DOOM_NES_FX_EDIT_DEFAULT;
    int32_t counts[7] = {0};
    fm1_doom_edit_init(&edit);
    counts[0] = 3;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH && edit.select_edges == 3);
    counts[0] = 0;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH && edit.select_edges == 0);
    counts[0] = 4;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX && edit.select_edges == 0);
    CHECK(!memcmp(&edit.value, &nes, sizeof(nes)));
    CHECK(!fm1_doom_edit_update(&edit, counts));
    counts[0] = 8;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH && !memcmp(&edit.value, &synth, sizeof(synth)));
    counts[0] = 4;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX);
    counts[0] = 0;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH);
    return 0;
}

static int test_select_preserves_independent_controls_in_both_banks(void)
{
    fm1_doom_edit edit;
    fm1_doom_music_edit synth, nes;
    int32_t counts[7] = {0};
    fm1_doom_edit_init(&edit);
    counts[1] = 4; counts[2] = 7; counts[6] = 8;
    CHECK(fm1_doom_edit_update(&edit, counts));
    synth = edit.value;
    counts[0] = 4;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX && edit.value.preset == 0);
    counts[1] += 8; counts[3] = 5; counts[5] = 7;
    CHECK(fm1_doom_edit_update(&edit, counts));
    nes = edit.value;
    counts[0] = 8;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH && !memcmp(&edit.value, &synth, sizeof(synth)));
    counts[0] = 12;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX && !memcmp(&edit.value, &nes, sizeof(nes)));
    return 0;
}

static int test_select_retains_bounded_backlog_and_int32_extremes(void)
{
    fm1_doom_edit edit;
    int32_t counts[7] = {19};
    unsigned step;
    fm1_doom_edit_init(&edit);
    for (step = 0; step < 3; ++step) {
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(edit.previous[0] == (step == 0 ? 8 : step == 1 ? 16 : 19));
        CHECK(edit.bank == FM1_DOOM_EDIT_SYNTH);
        CHECK(edit.select_edges == (step == 2 ? 3 : 0));
    }
    counts[0] = 20;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX && edit.select_edges == 0);
    fm1_doom_edit_init(&edit);
    edit.previous[0] = INT32_MAX - 2; counts[0] = INT32_MAX;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(edit.previous[0] == INT32_MAX && edit.select_edges == 2);
    counts[0] = INT32_MIN;
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(edit.previous[0] == INT32_MAX - 8 && edit.select_edges == -2);
    CHECK(edit.bank == FM1_DOOM_EDIT_NES_FX);
    return 0;
}

static int test_nes_presets_load_the_documented_controls_and_clamp(void)
{
    static const fm1_doom_music_edit presets[] = {
        {0, 0, {112, 0, 19, 0}}, {1, 1, {92, 24, 9, 0}},
        {2, 1, {76, 60, 9, 72}}, {3, 2, {84, 100, 29, 96}}
    };
    fm1_doom_edit edit;
    int32_t counts[7] = {4};
    unsigned step;
    fm1_doom_edit_init(&edit);
    CHECK(fm1_doom_edit_update(&edit, counts));
    CHECK(!memcmp(&edit.value, presets, sizeof(edit.value)));
    for (step = 1; step < 4; ++step) {
        counts[6] += 3;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.preset == step - 1);
        counts[6] += 1;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(!memcmp(&edit.value, presets + step, sizeof(edit.value)));
    }
    counts[6] += 4;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(!memcmp(&edit.value, presets + 3, sizeof(edit.value)));
    for (step = 3; step > 0; --step) {
        counts[6] -= 4;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(!memcmp(&edit.value, presets + step - 1, sizeof(edit.value)));
    }
    counts[6] -= 4;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(!memcmp(&edit.value, presets, sizeof(edit.value)));
    return 0;
}

static int test_nes_algorithm_clamps_and_knobs_consume_eight_edge_backlog(void)
{
    static const uint8_t knobs[] = {112, 0, 19, 0};
    fm1_doom_edit edit;
    int32_t counts[7] = {4};
    unsigned i, step;
    fm1_doom_edit_init(&edit);
    CHECK(fm1_doom_edit_update(&edit, counts));
    counts[1] = -4;
    CHECK(!fm1_doom_edit_update(&edit, counts));
    CHECK(edit.value.algorithm == 0 && edit.algorithm_edges == 0);
    for (step = 1; step <= 4; ++step) {
        counts[1] += 4;
        CHECK(fm1_doom_edit_update(&edit, counts) == (step < 4));
        CHECK(edit.value.algorithm == (step < 4 ? step : 3));
    }
    for (i = 2; i < 6; ++i) counts[i] = 19;
    for (step = 0; step < 3; ++step) {
        unsigned consumed = step == 0 ? 8 : step == 1 ? 16 : 19;
        CHECK(fm1_doom_edit_update(&edit, counts));
        for (i = 0; i < 4; ++i) {
            unsigned value = knobs[i] + consumed;
            CHECK(edit.previous[i + 2] == (int32_t)consumed);
            CHECK(edit.value.knob[i] == (value > 127 ? 127 : value));
        }
    }
    CHECK(!fm1_doom_edit_update(&edit, counts));
    return 0;
}

static int test_selectors_require_four_edges_per_detent(void)
{
    unsigned i;
    for (i = 0; i < 2; ++i) {
        fm1_doom_edit edit;
        int32_t counts[7] = {0};
        unsigned id = selectors[i];
        fm1_doom_edit_init(&edit);
        counts[id] = 1;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == 1);
        counts[id] = 3;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == 3);
        counts[id] = 4;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 1 && remainder(&edit, id) == 0);
        CHECK(!fm1_doom_edit_update(&edit, counts));

        fm1_doom_edit_init(&edit);
        counts[id] = -1;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == -1);
        counts[id] = -3;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == -3);
        counts[id] = -4;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 3 && remainder(&edit, id) == 0);
    }
    return 0;
}

static int test_selectors_wrap_in_both_directions(void)
{
    unsigned i, step;
    for (i = 0; i < 2; ++i) {
        fm1_doom_edit edit;
        int32_t counts[7] = {0};
        unsigned id = selectors[i];
        fm1_doom_edit_init(&edit);
        for (step = 1; step <= 4; ++step) {
            counts[id] += 4;
            CHECK(fm1_doom_edit_update(&edit, counts));
            CHECK(selected(&edit, id) == (step & 3u));
            CHECK(remainder(&edit, id) == 0);
        }
        for (step = 1; step <= 4; ++step) {
            counts[id] -= 4;
            CHECK(fm1_doom_edit_update(&edit, counts));
            CHECK(selected(&edit, id) == ((4u - step) & 3u));
            CHECK(remainder(&edit, id) == 0);
        }
    }
    return 0;
}

static int test_reversing_partial_detents_cancels_the_remainder(void)
{
    unsigned i;
    for (i = 0; i < 2; ++i) {
        fm1_doom_edit edit;
        int32_t counts[7] = {0};
        unsigned id = selectors[i];
        fm1_doom_edit_init(&edit);
        counts[id] = 3;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        counts[id] = 1;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == 1);
        counts[id] = 0;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 0 && remainder(&edit, id) == 0);
        counts[id] = -3;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        counts[id] = -4;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(selected(&edit, id) == 3 && remainder(&edit, id) == 0);
    }
    return 0;
}

static int test_each_knob_uses_one_edge_per_step_independently(void)
{
    static const uint8_t defaults[] = {16, 72, 32, 0};
    unsigned knob, i;
    for (knob = 0; knob < 4; ++knob) {
        fm1_doom_edit edit;
        int32_t counts[7] = {0};
        fm1_doom_edit_init(&edit);
        counts[knob + 2] = 1;
        CHECK(fm1_doom_edit_update(&edit, counts));
        for (i = 0; i < 4; ++i)
            CHECK(edit.value.knob[i] == defaults[i] + (i == knob));
        CHECK(edit.value.preset == 0 && edit.value.algorithm == 0);
        CHECK(!fm1_doom_edit_update(&edit, counts));
        counts[knob + 2] = 0;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(!memcmp(edit.value.knob, defaults, sizeof(defaults)));
    }
    return 0;
}

static int test_knobs_clamp_without_retaining_saturated_movement(void)
{
    unsigned knob;
    for (knob = 0; knob < 4; ++knob) {
        fm1_doom_edit edit;
        int32_t counts[7] = {0};
        unsigned id = knob + 2;
        fm1_doom_edit_init(&edit);
        edit.value.knob[knob] = 126;
        counts[id] = 8;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 127);
        counts[id] = 16;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 127 && edit.previous[id] == 16);
        counts[id] = 9;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 120);
        edit.value.knob[knob] = 1;
        counts[id] = 1;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 0);
        counts[id] = -7;
        CHECK(!fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 0 && edit.previous[id] == -7);
        counts[id] = -6;
        CHECK(fm1_doom_edit_update(&edit, counts));
        CHECK(edit.value.knob[knob] == 1);
    }
    return 0;
}

static int test_each_input_preserves_backlog_beyond_eight_edges(void)
{
    static const uint8_t defaults[] = {16, 72, 32, 0};
    static const int32_t consumed[] = {8, 16, 19};
    fm1_doom_edit edit;
    int32_t counts[7] = {0, 19, 19, 19, 19, 19, 19};
    unsigned step, id;
    fm1_doom_edit_init(&edit);
    for (step = 0; step < 3; ++step) {
        CHECK(fm1_doom_edit_update(&edit, counts));
        for (id = 1; id < 7; ++id) CHECK(edit.previous[id] == consumed[step]);
        for (id = 0; id < 4; ++id)
            CHECK(edit.value.knob[id] == defaults[id] + consumed[step]);
        CHECK(edit.value.algorithm == (step == 0 ? 2 : 0));
        CHECK(edit.value.preset == (step == 0 ? 2 : 0));
        CHECK(edit.algorithm_edges == (step == 2 ? 3 : 0));
        CHECK(edit.preset_edges == (step == 2 ? 3 : 0));
    }
    CHECK(!fm1_doom_edit_update(&edit, counts));
    /* A new opposite sample must still be consumed in bounded pieces. */
    for (id = 1; id < 7; ++id) counts[id] = -2;
    for (step = 0; step < 3; ++step) {
        CHECK(fm1_doom_edit_update(&edit, counts));
        for (id = 1; id < 7; ++id)
            CHECK(edit.previous[id] == (step == 0 ? 11 : step == 1 ? 3 : -2));
    }
    for (id = 0; id < 4; ++id)
        CHECK(edit.value.knob[id] == (defaults[id] > 2 ? defaults[id] - 2 : 0));
    CHECK(edit.value.algorithm == 0 && edit.value.preset == 0);
    CHECK(edit.algorithm_edges == -2 && edit.preset_edges == -2);
    CHECK(!fm1_doom_edit_update(&edit, counts));
    return 0;
}

static int test_int32_extremes_do_not_overflow_encoder_deltas(void)
{
    fm1_doom_edit edit;
    int32_t counts[7] = {0};
    unsigned id;
    fm1_doom_edit_init(&edit);
    for (id = 1; id < 7; ++id) {
        edit.previous[id] = INT32_MAX - 2;
        counts[id] = INT32_MAX;
    }
    CHECK(fm1_doom_edit_update(&edit, counts));
    for (id = 1; id < 7; ++id) CHECK(edit.previous[id] == INT32_MAX);
    CHECK(edit.algorithm_edges == 2 && edit.preset_edges == 2);
    for (id = 1; id < 7; ++id) counts[id] = INT32_MIN;
    CHECK(fm1_doom_edit_update(&edit, counts));
    for (id = 1; id < 7; ++id) CHECK(edit.previous[id] == INT32_MAX - 8);
    CHECK(edit.value.algorithm == 3 && edit.value.preset == 3);
    CHECK(edit.algorithm_edges == -2 && edit.preset_edges == -2);
    CHECK(edit.value.knob[0] == 10 && edit.value.knob[1] == 66);
    CHECK(edit.value.knob[2] == 26 && edit.value.knob[3] == 0);

    fm1_doom_edit_init(&edit);
    for (id = 1; id < 7; ++id) {
        edit.previous[id] = INT32_MIN + 2;
        counts[id] = INT32_MIN;
    }
    CHECK(fm1_doom_edit_update(&edit, counts));
    for (id = 1; id < 7; ++id) CHECK(edit.previous[id] == INT32_MIN);
    for (id = 1; id < 7; ++id) counts[id] = INT32_MAX;
    CHECK(fm1_doom_edit_update(&edit, counts));
    for (id = 1; id < 7; ++id) CHECK(edit.previous[id] == INT32_MIN + 8);
    CHECK(edit.value.algorithm == 1 && edit.value.preset == 1);
    CHECK(edit.algorithm_edges == 2 && edit.preset_edges == 2);
    CHECK(edit.value.knob[0] == 22 && edit.value.knob[1] == 78);
    CHECK(edit.value.knob[2] == 38 && edit.value.knob[3] == 8);
    return 0;
}

static int test_packed_fields_use_the_documented_bit_positions(void)
{
    fm1_doom_music_edit value = {2, 1, {1, 2, 3, 4}};
    unsigned knob;
    CHECK(fm1_doom_edit_pack(&value) == UINT32_C(0x6080c101));
    for (knob = 0; knob < 4; ++knob) {
        memset(&value, 0, sizeof(value));
        value.knob[knob] = 127;
        CHECK(fm1_doom_edit_pack(&value) == (UINT32_C(127) << (knob * 7)));
    }
    memset(&value, 0, sizeof(value));
    value.preset = 3;
    CHECK(fm1_doom_edit_pack(&value) == UINT32_C(0x30000000));
    value.preset = 0;
    value.algorithm = 3;
    CHECK(fm1_doom_edit_pack(&value) == UINT32_C(0xc0000000));
    return 0;
}

static int test_pack_masks_field_widths_without_mutating_controls(void)
{
    fm1_doom_music_edit value, before;
    memset(&value, 0xff, sizeof(value));
    before = value;
    CHECK(fm1_doom_edit_pack(&value) == UINT32_MAX);
    CHECK(!memcmp(&value, &before, sizeof(value)));
    return 0;
}

int main(void)
{
    CHECK(test_defaults_reset_all_encoder_state() == 0);
    CHECK(test_select_requires_four_net_edges_and_wraps_both_banks() == 0);
    CHECK(test_select_preserves_independent_controls_in_both_banks() == 0);
    CHECK(test_select_retains_bounded_backlog_and_int32_extremes() == 0);
    CHECK(test_nes_presets_load_the_documented_controls_and_clamp() == 0);
    CHECK(test_nes_algorithm_clamps_and_knobs_consume_eight_edge_backlog() == 0);
    CHECK(test_selectors_require_four_edges_per_detent() == 0);
    CHECK(test_selectors_wrap_in_both_directions() == 0);
    CHECK(test_reversing_partial_detents_cancels_the_remainder() == 0);
    CHECK(test_each_knob_uses_one_edge_per_step_independently() == 0);
    CHECK(test_knobs_clamp_without_retaining_saturated_movement() == 0);
    CHECK(test_each_input_preserves_backlog_beyond_eight_edges() == 0);
    CHECK(test_int32_extremes_do_not_overflow_encoder_deltas() == 0);
    CHECK(test_packed_fields_use_the_documented_bit_positions() == 0);
    CHECK(test_pack_masks_field_widths_without_mutating_controls() == 0);
    puts("FM-1 real-time sound edit encoder, backlog and packed controls passed");
    return 0;
}
