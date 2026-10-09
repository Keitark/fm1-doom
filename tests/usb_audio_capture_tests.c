#include "fm1_usb_audio.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned irq_depth, lock_depth, lock_calls;
unsigned fm1_usb_audio_test_irq_save(void)
{
    unsigned old = irq_depth;
    assert(!lock_depth);
    ++irq_depth;
    return old;
}
void fm1_usb_audio_test_irq_restore(unsigned old)
{
    assert(irq_depth == old + 1 && !lock_depth);
    irq_depth = old;
}
void fm1_usb_audio_test_lock(unsigned char *p)
{
    assert(p && irq_depth && !lock_depth);
    ++lock_depth; ++lock_calls;
}
void fm1_usb_audio_test_unlock(unsigned char *p)
{
    assert(p && lock_depth == 1 && irq_depth);
    --lock_depth;
}

static fm1_usb_audio_capture_diagnostics diagnostics(void)
{
    fm1_usb_audio_capture_diagnostics d;
    fm1_usb_audio_capture_get_diagnostics(&d);
    assert(d.fill <= 128);
    return d;
}
static int16_t sample(const uint8_t *p, unsigned i, unsigned ch)
{
    unsigned n = 4 * i + 2 * ch;
    return (int16_t)((unsigned)p[n] | ((unsigned)p[n + 1] << 8));
}
static int16_t identity(unsigned n) { return (int16_t)(1000 + n % 20000); }
static void push(unsigned first, unsigned n)
{
    int32_t pcm[128], saved[128];
    unsigned i;
    assert(n <= 64);
    for (i = 0; i < n; ++i) {
        pcm[2 * i] = (int32_t)identity(first + i) * 256;
        pcm[2 * i + 1] = -(int32_t)identity(first + i) * 256;
    }
    memcpy(saved, pcm, n * 2 * sizeof(*pcm));
    fm1_usb_audio_capture_push_pcm24(pcm, n);
    assert(!memcmp(saved, pcm, n * 2 * sizeof(*pcm)));
}
static void start(void)
{
    fm1_usb_audio_capture_init();
    assert(fm1_usb_audio_capture_stream(1));
}
static unsigned prepare(const uint8_t **p, unsigned *expected)
{
    const volatile unsigned *epoch;
    unsigned n = fm1_usb_audio_capture_prepare(1, p, &epoch, expected);
    assert(n == 172 || n == 176 || n == 180);
    assert(*epoch == *expected);
    return n;
}

static void profile(void)
{
    const uint8_t *p = fm1_usb_audio_capture_descriptor;
    unsigned off = 0, interfaces = 0, endpoints = 0, formats = 0, ac_bytes = 0;
    unsigned in_ac = 0;
    while (off < FM1_USB_AUDIO_DESCRIPTOR_BYTES) {
        unsigned len = p[off], type = p[off + 1];
        assert(len >= 2 && off + len <= FM1_USB_AUDIO_DESCRIPTOR_BYTES);
        if (type == 11) {
            assert(len == 8 && p[off + 2] == 2 && p[off + 3] == 2);
        } else if (type == 4) {
            unsigned i = p[off + 2], alt = p[off + 3];
            ++interfaces;
            assert(p[off + 5] == 1);
            assert((i == 2 && alt == 0 && p[off + 4] == 0) ||
                   (i == 3 && alt <= 1 && p[off + 4] == alt));
            in_ac = i == 2;
        } else if (type == 5) {
            ++endpoints;
            assert(len == 9 && p[off + 2] == 0x81 && p[off + 3] == 13);
            assert(p[off + 4] == 180 && !p[off + 5] && p[off + 6] == 1);
        } else if (type == 36 && in_ac) {
            ac_bytes += len;
            if (p[off + 2] == 1) assert(p[off + 5] == 30 && p[off + 8] == 3);
            if (p[off + 2] == 2) {
                assert(p[off + 3] == 3 && p[off + 4] == 3 && p[off + 5] == 6);
                assert(p[off + 7] == 2 && p[off + 8] == 3);
            }
            if (p[off + 2] == 3) assert(p[off + 3] == 4 && p[off + 7] == 3);
        } else if (type == 36 && p[off + 2] == 2) {
            ++formats;
            assert(len == 11 && p[off + 3] == 1 && p[off + 4] == 2);
            assert(p[off + 5] == 2 && p[off + 6] == 16 && p[off + 7] == 1);
            assert(((unsigned)p[off + 8] | ((unsigned)p[off + 9] << 8) |
                    ((unsigned)p[off + 10] << 16)) == 44100);
        }
        off += len;
    }
    assert(off == 99 && interfaces == 3 && endpoints == 1 && formats == 1 && ac_bytes == 30);
    assert(fm1_usb_audio_capture_request_valid(1,11,0,2,0));
    assert(fm1_usb_audio_capture_request_valid(1,11,0,3,0));
    assert(fm1_usb_audio_capture_request_valid(1,11,1,3,0));
    assert(fm1_usb_audio_capture_request_valid(0x81,10,0,3,1));
    assert(fm1_usb_audio_capture_request_valid(0x81,0,0,2,2));
    assert(!fm1_usb_audio_capture_request_valid(1,11,1,2,0));
    assert(!fm1_usb_audio_capture_request_valid(1,11,2,3,0));
    assert(!fm1_usb_audio_capture_request_valid(1,11,1,0x103,0));
    assert(!fm1_usb_audio_capture_request_valid(1,11,1,3,1));
    assert(!fm1_usb_audio_capture_request_valid(0x21,1,0,3,3));
    assert(!fm1_usb_audio_capture_request_valid(0x82,0,0,0x81,2));
}

static void conversion_and_bounds(void)
{
    const int32_t values[] = {INT32_MIN, INT32_MAX, -8388609, 8388608,
        -8388608, 8388352, -257, -255, 255, 257, 0};
    const int16_t expected[] = {-32768,32767,-32768,32767,-32768,32767,-1,0,0,1,0};
    int32_t pcm[128], saved[128];
    const uint8_t *p;
    unsigned i, epoch, n;
    fm1_usb_audio_capture_init();
    push(0,64); assert(diagnostics().pushed_frames == 0);
    assert(fm1_usb_audio_capture_stream(1));
    for (i=0;i<64;++i) {
        pcm[2*i] = values[i % 11]; pcm[2*i+1] = values[10 - i % 11];
    }
    memcpy(saved,pcm,sizeof(pcm));
    fm1_usb_audio_capture_push_pcm24(NULL,64);
    fm1_usb_audio_capture_push_pcm24(pcm,0);
    fm1_usb_audio_capture_push_pcm24(pcm,65);
    assert(diagnostics().fill == 0);
    fm1_usb_audio_capture_push_pcm24(pcm,64);
    assert(!memcmp(saved,pcm,sizeof(pcm)));
    n=prepare(&p,&epoch);
    for(i=0;i<n/4;++i) {
        assert(sample(p,i,0) == expected[i%11]);
        assert(sample(p,i,1) == expected[10-i%11]);
    }
    fm1_usb_audio_capture_complete(epoch,n);
    assert(diagnostics().pushed_frames == 64 && diagnostics().sent_frames == n/4);
}

static void busy_retry_and_epoch(void)
{
    const uint8_t *p, *sentinel = (const uint8_t *)(uintptr_t)1;
    const volatile unsigned *epochp = (const volatile unsigned *)(uintptr_t)2;
    uint8_t saved[180];
    unsigned epoch, untouched=1234, n, later;
    fm1_usb_audio_capture_diagnostics before, after;
    start(); push(0,64); before=diagnostics();
    assert(!fm1_usb_audio_capture_prepare(0,&sentinel,&epochp,&untouched));
    after=diagnostics();
    assert(sentinel==(const uint8_t *)(uintptr_t)1 && untouched==1234);
    assert(epochp==(const volatile unsigned *)(uintptr_t)2);
    assert(after.fill==before.fill && after.pending_bytes==0 && after.busy==1);
    n=prepare(&p,&epoch); memcpy(saved,p,n); before=diagnostics();
    assert(!fm1_usb_audio_capture_prepare(0,&sentinel,&epochp,&untouched));
    after=diagnostics();
    assert(after.fill==before.fill && after.pending_bytes==n && !memcmp(p,saved,n));
    fm1_usb_audio_capture_complete(epoch,0);
    push(64,64);
    assert(prepare(&p,&later)==n && later==epoch && !memcmp(p,saved,n));
    fm1_usb_audio_capture_complete(epoch,n-4);
    assert(diagnostics().short_writes==2 && diagnostics().submitted_packets==0);
    assert(prepare(&p,&later)==n && !memcmp(p,saved,n));
    fm1_usb_audio_capture_complete(epoch,n);
    assert(diagnostics().submitted_packets==1 && diagnostics().pending_bytes==0);
    n=prepare(&p,&epoch);
    assert(fm1_usb_audio_capture_stream(0)==0 && diagnostics().fill==0);
    assert(fm1_usb_audio_capture_stream(1)); push(1000,64);
    n=prepare(&p,&later); memcpy(saved,p,n);
    assert(later!=epoch);
    fm1_usb_audio_capture_complete(epoch,n);
    assert(diagnostics().pending_bytes==n && !memcmp(p,saved,n));
    fm1_usb_audio_capture_complete(later,n);
    fm1_usb_audio_capture_stop(); push(2000,64);
    assert(!fm1_usb_audio_capture_stream(1) && !diagnostics().armed && diagnostics().fill==0);
    fm1_usb_audio_capture_complete(later,n);
}

static void overflow_and_starvation(void)
{
    const uint8_t *p;
    unsigned epoch,n,i;
    start(); push(0,64); push(64,64); push(128,64);
    assert(diagnostics().overruns==64 && diagnostics().fill==128);
    n=prepare(&p,&epoch);
    for(i=0;i<n/4;++i) assert(sample(p,i,0)==identity(64+i));
    fm1_usb_audio_capture_complete(epoch,n);
    while(diagnostics().fill) {
        unsigned prior_fill=diagnostics().fill;
        n=prepare(&p,&epoch);
        for(i=0;i<n/4;++i) {
            if(i<prior_fill) assert(sample(p,i,0)!=0);
            else assert(sample(p,i,0)==0 && sample(p,i,1)==0);
        }
        fm1_usb_audio_capture_complete(epoch,n);
    }
    assert(diagnostics().underruns==1);
    n=prepare(&p,&epoch);
    for(i=0;i<n/4;++i) assert(sample(p,i,0)==0 && sample(p,i,1)==0);
    fm1_usb_audio_capture_complete(epoch,n);
    push(500,32); n=prepare(&p,&epoch);
    assert(diagnostics().fill==32);
    for(i=0;i<n/4;++i) assert(sample(p,i,0)==0);
    fm1_usb_audio_capture_complete(epoch,n);
    push(532,32); n=prepare(&p,&epoch);
    for(i=0;i<n/4;++i) assert(sample(p,i,0)==identity(500+i));
    fm1_usb_audio_capture_complete(epoch,n);
}

static void nominal_and_long_backpressure(void)
{
    const uint8_t *p;
    uint8_t saved[180];
    unsigned n,epoch,i,total=0,next=64;
    fm1_usb_audio_capture_diagnostics d;
    start(); push(0,64);
    for(i=0;i<10;++i) {
        n=prepare(&p,&epoch);
        assert(n==(i==9?180u:176u));
        total+=n/4; fm1_usb_audio_capture_complete(epoch,n);
        push(next,n/4); next+=n/4;
    }
    assert(total==441 && diagnostics().underruns==0 && diagnostics().overruns==0);
    start(); push(0,64); n=prepare(&p,&epoch); memcpy(saved,p,n);
    /* A disconnected/stalled host cannot block the IIS producer, allocate,
     * rewrite a rejected packet or grow the queue. Every displaced frame is
     * counted; after reconnection the pending packet precedes retained FIFO. */
    for(i=0;i<500;++i) {
        const uint8_t *ignored=p;
        const volatile unsigned *epoch_pointer;
        unsigned expected;
        push(64+64*i,64);
        assert(!fm1_usb_audio_capture_prepare(0,&ignored,&epoch_pointer,&expected));
        assert(!memcmp(p,saved,n));
        fm1_usb_audio_capture_complete(epoch,0);
    }
    d=diagnostics();
    assert(d.fill==128 && d.pending_bytes==n && d.pushed_frames==32064);
    assert(d.overruns==32064-n/4-128 && d.busy==500 && d.short_writes==500);
    assert(!d.submitted_packets);
    assert(prepare(&p,&epoch)==n && !memcmp(p,saved,n));
    fm1_usb_audio_capture_complete(epoch,n);
    n=prepare(&p,&epoch);
    for(i=0;i<n/4;++i) {
        assert(sample(p,i,0)==identity(32064-128+i));
        assert(sample(p,i,1)==-sample(p,i,0));
    }
    fm1_usb_audio_capture_complete(epoch,n);
}

/* Actual IIS producer bursts64 frames independently of the1000Hz USB SOF.
 * Verify every retained frame, including restart silence and counted drops.
 * Beyond startup, independent clocks +/-500ppm must remain lossless. */
static void clock_run(int ppm, unsigned phase_ns)
{
    uint64_t period=64000000000000000ULL/(44100ULL*(uint64_t)(1000000+ppm));
    uint64_t next=phase_ns;
    unsigned tick, produced=0, wanted=0, previous_over=0, settled_under=0, settled_over=0;
    const uint8_t *p;
    start();
    for(tick=0;tick<60000;++tick) {
        uint64_t now=(uint64_t)tick*1000000ULL;
        unsigned n,epoch,i;
        fm1_usb_audio_capture_diagnostics d;
        while(next<=now) { push(produced,64); produced+=64; next+=period; }
        d=diagnostics(); wanted+=d.overruns-previous_over; previous_over=d.overruns;
        n=prepare(&p,&epoch);
        for(i=0;i<n/4;++i) {
            int16_t l=sample(p,i,0),r=sample(p,i,1);
            if(l) { assert(l==identity(wanted)); assert(r==-l); ++wanted; }
            else assert(r==0);
        }
        fm1_usb_audio_capture_complete(epoch,n);
        d=diagnostics();
        if(tick==500) { settled_under=d.underruns; settled_over=d.overruns; }
        if(tick>500) { assert(d.underruns==settled_under); assert(d.overruns==settled_over); }
    }
    {
        fm1_usb_audio_capture_diagnostics d=diagnostics();
        assert(wanted+d.fill==d.pushed_frames);
        printf("clock %+dppm phase%u: fill%u under%u over%u silent%u frames%u\n",
               ppm,phase_ns,d.fill,d.underruns,d.overruns,d.silent_frames,d.sent_frames);
    }
}

int main(void)
{
    profile(); conversion_and_bounds(); busy_retry_and_epoch(); overflow_and_starvation();
    nominal_and_long_backpressure();
    clock_run(0,0); clock_run(500,0); clock_run(-500,0);
    clock_run(0,700000); clock_run(500,1400000); clock_run(-500,300000);
    assert(!irq_depth && !lock_depth && lock_calls);
    puts("capture-only UAC profile, conversion, retries, epochs, FIFO and64-frame clock tests passed");
    return 0;
}
