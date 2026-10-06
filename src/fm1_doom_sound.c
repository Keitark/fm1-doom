/* Bounded two-voice shareware SFX output using the working NES I2S route.
 * Exact PCM8/Rice or legacy IMA stays in XIP; no FMD/cache work occurs in IRQs. */
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
#ifndef FM1_DOOM_SOUND_TEST
#include "fm1_doom_memory.h"
#include "fm1_usb_audio.h"
#include "fm1_usb_audio_target.h"
#endif
#include "fm1_board.h"
#include "fm1_volume.h"
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

static void rice_block_start(fm1_doom_sound_voice *voice)
{
    /* Each 256-sample block begins with an unsigned PCM8 predictor and k.
     * The whole immutable stream is validated before this state is used. */
    voice->predictor = ((int32_t)voice->next[0] - 128) * 256;
    voice->index = voice->next[1];
    voice->next += 2;
    voice->high = 0;
    voice->block_left = (uint16_t)((voice->samples_left < 256u
                                  ? voice->samples_left : 256u) - 1u);
}

static unsigned rice_bit(fm1_doom_sound_voice *voice)
{
    unsigned value = (*voice->next >> voice->high) & 1u;
    if (++voice->high == 8u) { ++voice->next; voice->high = 0; }
    return value;
}

static unsigned rice_delta(fm1_doom_sound_voice *voice)
{
    unsigned quotient = 0, value, count, i;
    /* Seven one bits escape to an 8-bit zigzag delta. Normal codes have
     * at most 6 unary bits, one zero and k<=7 remainder bits: <=15 total. */
    while (quotient < 7u && rice_bit(voice)) ++quotient;
    count = quotient == 7u ? 8u : voice->index;
    value = quotient == 7u ? 0u : quotient << voice->index;
    for (i = 0; i < count; ++i) value |= rice_bit(voice) << i;
    return value;
}

static int validate_rice(const uint8_t *data, size_t bytes, uint32_t declared)
{
    fm1_doom_sound_voice checked;
    uint32_t remaining = declared;
    memset(&checked, 0, sizeof(checked));
    checked.next = data + 8;
    checked.end = data + bytes;
    while (remaining) {
        unsigned samples = remaining < 256u ? remaining : 256u, i;
        if ((size_t)(checked.end - checked.next) < 2u || checked.next[1] > 7u) return -1;
        checked.index = checked.next[1]; checked.next += 2; checked.high = 0;
        for (i = 1; i < samples; ++i) {
            unsigned quotient = 0, value, count, bit;
            do {
                if (checked.next == checked.end) return -1;
                bit = rice_bit(&checked);
                if (!bit) break;
            } while (++quotient < 7u);
            count = quotient == 7u ? 8u : checked.index;
            value = quotient == 7u ? 0u : quotient << checked.index;
            for (bit = 0; bit < count; ++bit) {
                if (checked.next == checked.end) return -1;
                value |= rice_bit(&checked) << bit;
            }
            if (value > 255u) return -1;
        }
        /* Padding aligns the next predictor; reject hidden/trailing data. */
        if (checked.high) {
            if ((*checked.next >> checked.high) != 0u) return -1;
            ++checked.next;
        }
        remaining -= samples;
    }
    return checked.next == checked.end ? 0 : -1;
}

int fm1_doom_sound_voice_start(fm1_doom_sound_voice *voice,
                              const uint8_t *data, size_t bytes)
{
    size_t offset;
    uint32_t rate, declared;
    if (!voice) return -1;
    memset(voice, 0, sizeof(*voice));
    if (!data || bytes < 8u || data[0] != 3u ||
        (data[1] != 128u && data[1] != 129u)) return -1;
    rate = le16(data + 2);
    declared = le16(data + 4) | le16(data + 6) << 16;
    if ((rate != 8000u && rate != 11025u) || !declared || declared > 1000000u) return -1;
    if (data[1] == 129u) {
        if (validate_rice(data, bytes, declared)) return -1;
        voice->encoding = 1;
        voice->samples_left = declared;
    } else {
        if (bytes < 12u) return -1;
        /* Preserve legacy WHX IMA's bounded padded final block. */
        for (offset = 8; offset < bytes;) {
            size_t block = bytes - offset;
            if (block > 128u) block = 128u;
            if (block < 4u || (block - 4u) % 4u ||
                data[offset + 2u] > 88u || data[offset + 3u]) return -1;
            offset += block;
        }
    }
    voice->next = data + 8;
    voice->end = data + bytes;
    voice->step = (rate << 16) / FM1_DOOM_SOUND_OUTPUT_RATE;
    voice->left = voice->right = 127;
    voice->playing = 1;
    if (voice->encoding) rice_block_start(voice);
    else block_start(voice);
    return 0;
}

static void advance(fm1_doom_sound_voice *voice)
{
    unsigned nibble;
    int32_t step, delta, index;
    if (voice->encoding) {
        unsigned value;
        int32_t signed_delta, sample;
        if (!--voice->samples_left) {
            if (voice->high) { ++voice->next; voice->high = 0; }
            voice->playing = 0;
            return;
        }
        if (!voice->block_left) {
            if (voice->high) ++voice->next; /* next block is byte-aligned */
            rice_block_start(voice);
            return;
        }
        value = rice_delta(voice);
        signed_delta = (int32_t)(value >> 1);
        if (value & 1u) signed_delta = -signed_delta - 1;
        sample = (int32_t)((uint32_t)(voice->predictor / 256 + 128 + signed_delta) & 255u);
        voice->predictor = (sample - 128) * 256;
        --voice->block_left;
        return;
    }
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
    fm1_doom_music_begin_block();
    for (i = 0; i < 64u; ++i) {
        int16_t music_left, music_right;
        int32_t left, right;
        fm1_doom_music_sample_stereo(&music_left, &music_right);
        left = (int32_t)music_left * 256;
        right = (int32_t)music_right * 256;
        for (channel = 0; channel < FM1_DOOM_SOUND_VOICES; ++channel) {
            fm1_doom_sound_voice *voice = &voices[channel];
            if (!voice->playing) continue;
            left += voice->predictor * voice->left * FM1_DOOM_SOUND_EFFECT_PCM24_GAIN;
            right += voice->predictor * voice->right * FM1_DOOM_SOUND_EFFECT_PCM24_GAIN;
            voice->phase += voice->step;
            if (voice->phase >= 65536u) {
                voice->phase -= 65536u;
                advance(voice);
            }
        }
        /* Original PCM16 is untouched. Effects now use half the initial Q7
         * output gain; music and the physical master gain are independent. */
        stereo[2u * i] = clip24(left);
        stereo[2u * i + 1u] = clip24(right);
    }
}

extern int snd_channels;
volatile int fm1_doom_sound_error;
volatile uint32_t fm1_doom_sound_irqs, fm1_doom_sound_frames;
volatile uint32_t fm1_doom_sound_started;
static fm1_doom_sound_voice voices[FM1_DOOM_SOUND_VOICES];
/* The audio lock serializes this CPU-only block. Publish to the SDK DMA half
 * only after both rendering and physical master scaling have completed. */
static int32_t render_block[128];
static fm1_audio_startup envelope;
static fm1_volume master_volume;
/* Bits0..6 are the knob target; bit30 marks ADC lifecycle, bit31 speaker mute. */
static volatile uint32_t volume_target_q7;
#define SPEAKER_MUTED_BIT 0x80000000u
#define VOLUME_STARTED_BIT 0x40000000u
static struct iis_platform_data audio_pd; /* SDK retains this pointer. */
static spinlock_t audio_lock, volume_lock;
/* Keep the OPL renderer off CPU0's CDC, scanner and timer interrupt path. */
enum { AUDIO_IRQ_CPU = 1 };
static volatile unsigned audio_opened, audio_enabled, audio_irq_registered;
static volatile uint32_t max_irq_us;

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

static unsigned volume_take(void)
{
    unsigned flags;
    local_irq_save(flags);
    arch_spin_lock(&volume_lock);
    return flags;
}

static void volume_release(unsigned flags)
{
    arch_spin_unlock(&volume_lock);
    local_irq_restore(flags);
}

#ifdef FM1_DOOM_SOUND_TEST
#define volume_hardware_read fm1_sound_test_hardware_read
#else
static uint32_t volume_hardware_read(uint32_t address)
{
    return *(volatile uint32_t *)(uintptr_t)address;
}
#endif

#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void fm1_doom_sound_get_volume_hardware(fm1_doom_volume_hardware *hardware)
{
    unsigned flags;
    if (!hardware) return;
    flags = volume_take();
    hardware->adc_con = volume_hardware_read(0x13100u);
    hardware->adc_res = volume_hardware_read(0x13104u);
    hardware->pb_dir = volume_hardware_read(0x50048u);
    hardware->pb_die = volume_hardware_read(0x5004cu);
    hardware->pb_pu = volume_hardware_read(0x50050u);
    hardware->pb_pd = volume_hardware_read(0x50054u);
    hardware->pb_hd0 = volume_hardware_read(0x50058u);
    hardware->pb_hd1 = volume_hardware_read(0x5005cu);
    hardware->pb_dieh = volume_hardware_read(0x50060u);
    hardware->wla_con0 = volume_hardware_read(0x11900u);
    hardware->pll_con1 = volume_hardware_read(0x119a4u);
    hardware->raw = master_volume.raw;
    hardware->accepted = master_volume.accepted;
    hardware->target = volume_target_q7 & 127u;
    hardware->gain = *(const volatile uint8_t *)&envelope.gain_q7;
    hardware->running = master_volume.running;
    hardware->valid = master_volume.valid;
    hardware->waiting = master_volume.waiting;
    hardware->samples = master_volume.samples;
    hardware->errors = master_volume.errors;
    volume_release(flags);
}

int fm1_doom_sound_volume_start(void)
{
    unsigned flags = volume_take();
    int rc;
    /* The same control word carries lifecycle state without another RAM
     * allocation. Failed initialization/conversion remains observable until
     * explicit shutdown; later sound initialization must not reset it. */
    if (volume_target_q7 & VOLUME_STARTED_BIT) {
        rc = master_volume.running ? 0 : -1;
    } else {
        rc = fm1_volume_start(&master_volume);
        volume_target_q7 = VOLUME_STARTED_BIT |
            (FM1_DOOM_BOOT_MUTED ? SPEAKER_MUTED_BIT : 0);
    }
    volume_release(flags);
    return rc;
}

void fm1_doom_sound_volume_tick(void)
{
    unsigned flags = volume_take();
    fm1_volume_tick(&master_volume);
    volume_target_q7 = (volume_target_q7 & (SPEAKER_MUTED_BIT | VOLUME_STARTED_BIT)) |
        (master_volume.valid ? master_volume.target : 0);
    volume_release(flags);
}

void fm1_doom_sound_set_speaker_muted(unsigned muted)
{
    unsigned flags = volume_take();
    volume_target_q7 = (volume_target_q7 & ~SPEAKER_MUTED_BIT) |
        (muted ? SPEAKER_MUTED_BIT : 0);
    volume_release(flags);
}

unsigned fm1_doom_sound_speaker_is_muted(void)
{
    return (volume_target_q7 & SPEAKER_MUTED_BIT) != 0;
}

unsigned fm1_doom_sound_lock(void) { return take(); }
void fm1_doom_sound_unlock(unsigned flags) { release(flags); }

void fm1_doom_sound_toggle_music_mode(void)
{
    unsigned flags = take();
    fm1_doom_music_toggle_synth_mode();
    release(flags);
}

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
    const volatile fm1_volume *volume = &master_volume;
    const volatile fm1_audio_startup *gain = &envelope;
    if (!diagnostics) return;
    diagnostics->ready = audio_opened && audio_enabled && !fm1_doom_sound_error;
    diagnostics->error = fm1_doom_sound_error;
    diagnostics->irq_count = fm1_doom_sound_irqs;
    diagnostics->output_frames = fm1_doom_sound_frames;
    diagnostics->sfx_started = fm1_doom_sound_started;
    diagnostics->max_irq_us = max_irq_us;
    diagnostics->volume_raw = volume->raw;
    diagnostics->volume_gain = gain->gain_q7;
    diagnostics->volume_valid = volume->valid;
    diagnostics->volume_errors = volume->errors;
    diagnostics->volume_samples = volume->samples;
    diagnostics->volume_target = (uint8_t)(volume_target_q7 & 127u);
    diagnostics->speaker_muted = (uint8_t)fm1_doom_sound_speaker_is_muted();
    diagnostics->synth_mode = fm1_doom_music_get_synth_mode();
    diagnostics->active_voices = 0;
    for (i = 0; i < FM1_DOOM_SOUND_VOICES; ++i)
        if (*(const volatile uint8_t *)&voices[i].playing) ++diagnostics->active_voices;
}

static void output(void *unused, u8 *data, int len, u8 channel)
{
    uint32_t control;
    (void)unused;
    if (!data || len != 512 || channel != 3 || ((uintptr_t)data & 3u)) {
        if (data && len > 0) memset(data, 0, (size_t)(len < 512 ? len : 512));
        fm1_doom_sound_error = -42;
        return;
    }
    fm1_doom_sound_mix(voices, render_block);
#ifndef FM1_DOOM_SOUND_TEST
    /* Native-rate recording follows music/effects balance, before physical
     * master volume. The tap neither changes DMA samples nor takes USB locks. */
    fm1_usb_audio_capture_push_pcm24(render_block, 64u);
#endif
    /* One coherent control snapshot. CPU0 polls the ADC under volume_lock;
     * render work never delays its 2 ms sampling cadence. */
    control = volume_target_q7;
    envelope.target_q7 = control & SPEAKER_MUTED_BIT ? 0 : (uint8_t)(control & 127u);
    fm1_audio_startup_process24(&envelope, render_block);
    memcpy(data, render_block, sizeof(render_block));
#ifndef FM1_DOOM_SOUND_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
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
#ifndef FM1_DOOM_SOUND_TEST
    fm1_usb_audio_target_stop();
#endif
    if (audio_irq_registered) bit_clr_ie(IRQ_ALNK_IDX, AUDIO_IRQ_CPU);
    flags = take();
    audio_enabled = 0;
    memset(voices, 0, sizeof(voices));
    fm1_doom_music_stop_locked();
    envelope.gain_q7 = 0;
    envelope.target_q7 = 0;
    release(flags);
    flags = volume_take();
    fm1_volume_stop(&master_volume);
    volume_target_q7 &= SPEAKER_MUTED_BIT;
    volume_release(flags);
    if (audio_irq_registered) {
        unrequest_irq(IRQ_ALNK_IDX, AUDIO_IRQ_CPU);
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
    envelope.target_q7 = 0;
    rc = iis_open(&audio_pd, 0);
    if (rc) {
        fm1_doom_sound_error = rc;
        fm1_doom_sound_shutdown();
        return rc;
    }
    audio_opened = 1;
    iis_set_dec_data_handler(0, output, 0);
    rc = iis_set_sample_rate(FM1_DOOM_SOUND_OUTPUT_RATE, 0);
    if (rc) {
        fm1_doom_sound_error = rc;
        fm1_doom_sound_shutdown();
        return rc;
    }
    /* Match newer UAC NES hardware startup: open IIS and configure its rate
     * before ADC initialization, then enable ALINK. Preserve an explicitly
     * primed ADC; ownership/conversion errors remain separate and fail muted. */
    fm1_doom_sound_volume_start();
    flags = take();
#ifndef FM1_DOOM_SOUND_TEST
    {
        size_t samples;
        int16_t *delay = fm1_doom_reverb_buffer(&samples);
        fm1_doom_music_set_reverb_buffer(delay, samples);
    }
#endif
    audio_enabled = 1;
    release(flags);
    bit_clr_ie(IRQ_ALNK_IDX, 0);
    request_irq(IRQ_ALNK_IDX, 3, audio_isr, AUDIO_IRQ_CPU);
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
    for (i = 0; i < fm1_doom_sound_entry_count; ++i)
        if (!strcmp(name, fm1_doom_sound_entries[i].name)) return &fm1_doom_sound_entries[i];
    /* Compatibility for legacy three-sound WHX banks. Native banks can supply
     * exact distinct samples and always take precedence over these fallbacks. */
    if (!strcmp(name, "noway")) name = "oof";
    else if (!strcmp(name, "swtchx")) name = "swtchn";
    else return 0;
    for (i = 0; i < fm1_doom_sound_entry_count; ++i)
        if (!strcmp(name, fm1_doom_sound_entries[i].name)) {
            /* The switch fallback is only for legacy WHX banks; native
             * PCM8 banks must supply the distinct original exit switch. */
            if (!strcmp(sfx->name, "swtchx") &&
                fm1_doom_sound_entries[i].data[1] != 128u) return 0;
            return &fm1_doom_sound_entries[i];
        }
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
