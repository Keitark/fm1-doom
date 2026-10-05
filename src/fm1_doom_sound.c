/* Bounded two-voice shareware SFX output using the working NES I2S route.
 * Generated ADPCM samples stay in XIP flash; no FMD/cache work occurs in IRQs. */
#ifdef FM1_DOOM_SOUND_TEST
#include "fake_sound_sdk.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/spinlock.h"
#include "generic/jiffies.h"
#include "asm/iis.h"
#endif
#include "fm1_doom_sound.h"
#include "fm1_doom_music.h"
#include "fm1_board.h"
#include <string.h>

/* IMA ADPCM tables. The private WHX bank uses 128-byte blocks: signed-16
 * predictor, index, reserved zero, followed by low/high 4-bit samples. */
static const uint16_t step_table[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
    50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
    230,253,279,307,337,371,408,449,494,544,598,658,724,796,
    876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
    2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
    7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
    18500,20350,22385,24623,27086,29794,32767
};
static const int8_t index_delta[8] = {-1,-1,-1,-1,2,4,6,8};

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static void block_start(fm1_doom_sound_voice *voice)
{
    size_t bytes = (size_t)(voice->end - voice->next);
    uint32_t value = le16(voice->next);
    if (bytes > 128u) bytes = 128u;
    voice->predictor = value >= 32768u ? (int32_t)value - 65536 : (int32_t)value;
    voice->index = voice->next[2];
    voice->high = 0;
    voice->block_left = (uint16_t)((bytes - 4u) * 2u);
    voice->next += 4;
}

int fm1_doom_sound_voice_start(fm1_doom_sound_voice *voice,
                              const uint8_t *data, size_t bytes)
{
    size_t offset;
    uint32_t rate, declared;
    if (!voice) return -1;
    memset(voice, 0, sizeof(*voice));
    if (!data || bytes < 12u || data[0] != 3u || data[1] != 128u) return -1;
    rate = le16(data + 2);
    declared = le16(data + 4) | le16(data + 6) << 16;
    if ((rate != 8000u && rate != 11025u) || !declared || declared > 1000000u) return -1;
    /* Validate every block before publishing a voice to the IRQ. */
    for (offset = 8; offset < bytes;) {
        size_t block = bytes - offset;
        if (block > 128u) block = 128u;
        if (block < 4u || (block - 4u) % 4u ||
            data[offset + 2u] > 88u || data[offset + 3u]) return -1;
        offset += block;
    }
    voice->next = data + 8;
    voice->end = data + bytes;
    voice->step = (rate << 16) / FM1_DOOM_SOUND_OUTPUT_RATE;
    voice->left = voice->right = 127;
    voice->playing = 1;
    block_start(voice);
    return 0;
}

static void advance(fm1_doom_sound_voice *voice)
{
    unsigned nibble;
    int32_t step, delta, index;
    if (!voice->block_left) {
        if (voice->next == voice->end) { voice->playing = 0; return; }
        block_start(voice);
        return;
    }
    nibble = (*voice->next >> (voice->high ? 4 : 0)) & 15u;
    if (voice->high) ++voice->next;
    voice->high ^= 1u;
    --voice->block_left;
    step = step_table[voice->index];
    delta = step >> 3;
    if (nibble & 1u) delta += step >> 2;
    if (nibble & 2u) delta += step >> 1;
    if (nibble & 4u) delta += step;
    voice->predictor += (nibble & 8u) ? -delta : delta;
    if (voice->predictor > 32767) voice->predictor = 32767;
    if (voice->predictor < -32768) voice->predictor = -32768;
    index = (int32_t)voice->index + index_delta[nibble & 7u];
    voice->index = (uint8_t)(index < 0 ? 0 : index > 88 ? 88 : index);
}

static int32_t clip24(int32_t value)
{
    return value < -8388608 ? -8388608 : value > 8388607 ? 8388607 : value;
}

void fm1_doom_sound_mix(fm1_doom_sound_voice voices[FM1_DOOM_SOUND_VOICES],
                       int32_t stereo[128])
{
    unsigned i, channel;
    for (i = 0; i < 64u; ++i) {
        int16_t music_left, music_right;
        int32_t left, right;
        fm1_doom_music_sample_stereo(&music_left, &music_right);
        left = (int32_t)music_left * 128;
        right = (int32_t)music_right * 128;
        for (channel = 0; channel < FM1_DOOM_SOUND_VOICES; ++channel) {
            fm1_doom_sound_voice *voice = &voices[channel];
            if (!voice->playing) continue;
            left += voice->predictor * voice->left;
            right += voice->predictor * voice->right;
            voice->phase += voice->step;
            if (voice->phase >= 65536u) {
                voice->phase -= 65536u;
                advance(voice);
            }
        }
        /* Q7 volume and signed-16 -> signed-24 scaling combine to *2. */
        stereo[2u * i] = clip24(left * 2);
        stereo[2u * i + 1u] = clip24(right * 2);
    }
}

extern int snd_channels;
volatile int fm1_doom_sound_error;
volatile uint32_t fm1_doom_sound_irqs, fm1_doom_sound_frames;
volatile uint32_t fm1_doom_sound_started;
static fm1_doom_sound_voice voices[FM1_DOOM_SOUND_VOICES];
static fm1_audio_startup envelope;
static struct iis_platform_data audio_pd; /* SDK retains this pointer. */
static spinlock_t audio_lock;
static unsigned audio_opened, audio_enabled, audio_irq_registered;
static uint32_t max_irq_us;

static unsigned take(void)
{
    unsigned flags;
    local_irq_save(flags);
    arch_spin_lock(&audio_lock);
    return flags;
}

static void release(unsigned flags)
{
    arch_spin_unlock(&audio_lock);
    local_irq_restore(flags);
}

unsigned fm1_doom_sound_lock(void) { return take(); }
void fm1_doom_sound_unlock(unsigned flags) { release(flags); }

int fm1_doom_sound_is_ready(void)
{
    unsigned flags = take();
    int ready = audio_opened && audio_enabled && !fm1_doom_sound_error;
    release(flags);
    return ready;
}

void fm1_doom_sound_get_diagnostics(fm1_doom_sound_diagnostics *diagnostics)
{
    unsigned i;
    if (!diagnostics) return;
    diagnostics->ready = audio_opened && audio_enabled && !fm1_doom_sound_error;
    diagnostics->error = fm1_doom_sound_error;
    diagnostics->irq_count = fm1_doom_sound_irqs;
    diagnostics->output_frames = fm1_doom_sound_frames;
    diagnostics->sfx_started = fm1_doom_sound_started;
    diagnostics->max_irq_us = max_irq_us;
    diagnostics->active_voices = 0;
    for (i = 0; i < FM1_DOOM_SOUND_VOICES; ++i)
        if (voices[i].playing) ++diagnostics->active_voices;
}

static void output(void *unused, u8 *data, int len, u8 channel)
{
    (void)unused;
    if (!data || len != 512 || channel != 3 || ((uintptr_t)data & 3u)) {
        if (data && len > 0) memset(data, 0, (size_t)(len < 512 ? len : 512));
        fm1_doom_sound_error = -42;
        return;
    }
    fm1_doom_sound_mix(voices, (int32_t *)data);
    fm1_audio_startup_process24(&envelope, (int32_t *)data);
    fm1_doom_sound_frames += 64u;
}

___interrupt
static void audio_isr(void)
{
    unsigned flags = take();
    if (audio_enabled) {
        uint32_t before = (uint32_t)jiffies_half_msec();
        uint32_t duration;
        iis_irq_handler(0);
        duration = ((uint32_t)jiffies_half_msec() - before) * 500u;
        if (duration > max_irq_us) max_irq_us = duration;
        ++fm1_doom_sound_irqs;
    }
    release(flags);
}

void fm1_doom_sound_shutdown(void)
{
    unsigned flags;
    if (audio_irq_registered) bit_clr_ie(IRQ_ALNK_IDX, 0);
    flags = take();
    audio_enabled = 0;
    memset(voices, 0, sizeof(voices));
    fm1_doom_music_stop_locked();
    release(flags);
    if (audio_irq_registered) {
        unrequest_irq(IRQ_ALNK_IDX, 0);
        audio_irq_registered = 0;
    }
    if (audio_opened) {
        iis_channel_off(8, 0);
        iis_close(0);
        audio_opened = 0;
    }
}

int fm1_doom_sound_init(void)
{
    fm1_doom_sound_voice checked;
    unsigned i, flags;
    int rc;
    if (audio_opened) return 0;
    fm1_doom_sound_error = 0;
    fm1_doom_sound_irqs = fm1_doom_sound_frames = fm1_doom_sound_started = 0;
    max_irq_us = 0;
    for (i = 0; i < fm1_doom_sound_entry_count; ++i) {
        const fm1_doom_sound_entry *entry = &fm1_doom_sound_entries[i];
        if (fm1_doom_sound_voice_start(&checked, entry->data, entry->bytes)) {
            fm1_doom_sound_error = -40;
            return -40;
        }
    }
    memset(&audio_pd, 0, sizeof(audio_pd));
    audio_pd.port_sel = IIS_PORTC;
    audio_pd.channel_out = audio_pd.data_width = 8;
    audio_pd.mclk_output = 1;
    audio_pd.sr_points = 128;
    memset(voices, 0, sizeof(voices));
    fm1_audio_startup_reset(&envelope);
    rc = iis_open(&audio_pd, 0);
    if (rc) { fm1_doom_sound_error = rc; return rc; }
    audio_opened = 1;
    iis_set_dec_data_handler(0, output, 0);
    rc = iis_set_sample_rate(FM1_DOOM_SOUND_OUTPUT_RATE, 0);
    if (rc) {
        fm1_doom_sound_error = rc;
        fm1_doom_sound_shutdown();
        return rc;
    }
    flags = take();
    audio_enabled = 1;
    release(flags);
    request_irq(IRQ_ALNK_IDX, 3, audio_isr, 0);
    audio_irq_registered = 1;
    iis_channel_on(8, 0);
    snd_channels = FM1_DOOM_SOUND_VOICES;
    return 0;
}

static const fm1_doom_sound_entry *lookup(sfxinfo_t *sfx)
{
    unsigned i, links = 0;
    const char *name;
    if (!sfx) return 0;
    while (sfx->link) {
        if (++links > 4u) return 0;
        sfx = sfx->link;
    }
    name = sfx->name;
    if (!strcmp(name, "noway")) name = "oof";
    if (!strcmp(name, "swtchx")) name = "swtchn";
    for (i = 0; i < fm1_doom_sound_entry_count; ++i)
        if (!strcmp(name, fm1_doom_sound_entries[i].name)) return &fm1_doom_sound_entries[i];
    return 0;
}

static boolean module_init(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    return fm1_doom_sound_init() == 0;
}

static int lump(sfxinfo_t *sfx)
{
    const fm1_doom_sound_entry *entry = lookup(sfx);
    return entry ? (int)(entry - fm1_doom_sound_entries) + 1 : -1;
}

static void params(int channel, int vol, int sep)
{
    unsigned flags;
    if (channel < 0 || (unsigned)channel >= FM1_DOOM_SOUND_VOICES) return;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 254) sep = 254;
    flags = take();
    voices[channel].left = (uint8_t)(vol * (254 - sep) / 254);
    voices[channel].right = (uint8_t)(vol * sep / 254);
    release(flags);
}

static int start(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    const fm1_doom_sound_entry *entry = lookup(sfx);
    fm1_doom_sound_voice next;
    unsigned flags;
    if (!entry || channel < 0 || (unsigned)channel >= FM1_DOOM_SOUND_VOICES ||
        fm1_doom_sound_voice_start(&next, entry->data, entry->bytes)) return -1;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 254) sep = 254;
    next.left = (uint8_t)(vol * (254 - sep) / 254);
    next.right = (uint8_t)(vol * sep / 254);
    flags = take();
    if (!audio_enabled || fm1_doom_sound_error) { release(flags); return -1; }
    voices[channel] = next;
    ++fm1_doom_sound_started;
    release(flags);
    return channel;
}

static void stop(int channel)
{
    unsigned flags;
    if (channel < 0 || (unsigned)channel >= FM1_DOOM_SOUND_VOICES) return;
    flags = take();
    voices[channel].playing = 0;
    release(flags);
}

static boolean playing(int channel)
{
    unsigned flags;
    boolean result;
    if (channel < 0 || (unsigned)channel >= FM1_DOOM_SOUND_VOICES) return false;
    flags = take();
    result = audio_enabled && !fm1_doom_sound_error && voices[channel].playing;
    release(flags);
    return result;
}

static void update(void)
{
    if (fm1_doom_sound_error && audio_opened) fm1_doom_sound_shutdown();
}

static snddevice_t devices[] = {SNDDEVICE_SB};
sound_module_t fm1_sound_module = {
    devices, 1, module_init, fm1_doom_sound_shutdown, lump, update,
    params, start, stop, playing, 0
};
