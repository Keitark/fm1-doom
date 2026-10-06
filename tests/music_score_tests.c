#include "fm1_doom_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
extern const uint8_t fm1_doom_music_score[];
extern const uint32_t fm1_doom_music_score_len;

#ifdef FM1_TARGET_PI32V2
static unsigned backend_locked, backend_ready;
unsigned fm1_doom_sound_lock(void){CHECK(!backend_locked);backend_locked=1;return 7;}
void fm1_doom_sound_unlock(unsigned flags){CHECK(flags==7 && backend_locked);backend_locked=0;}
int fm1_doom_sound_is_ready(void){CHECK(!backend_locked);return backend_ready;}
static void module_tests(void)
{
    void *handle;
    CHECK(!fm1_music_module.Init());backend_ready=1;CHECK(fm1_music_module.Init());
    CHECK(!fm1_music_module.RegisterSong(0,1));
    handle=fm1_music_module.RegisterSong(0,0);CHECK(handle);
    fm1_music_module.SetMusicVolume(127);fm1_music_module.PlaySong(handle,true);
    CHECK(fm1_music_module.MusicIsPlaying());
    fm1_music_module.PauseMusic();CHECK(!fm1_doom_music_sample());
    fm1_music_module.ResumeMusic();fm1_music_module.StopSong();
    CHECK(!fm1_music_module.MusicIsPlaying());fm1_music_module.Shutdown();
    CHECK(!backend_locked);
}
#endif

static unsigned literal_score(uint8_t *output,const uint8_t *events,unsigned length)
{
    unsigned i;
    memcpy(output,"FMSC",4);output[4]=(uint8_t)length;output[5]=output[6]=output[7]=0;
    output[8]=255;output[9]=0;output[10]=140;output[11]=0;
    output[12]=(uint8_t)length;output[13]=output[14]=output[15]=0;
    for(i=0;i<255;i++){output[16+2*i]=(uint8_t)i;output[17+2*i]=255;}
    memcpy(output+526,events,length);return 526+length;
}

static void synthetic_tests(void)
{
    /* Original guitar A4 for140ticks. DOS OPL2 ignores MIDI pan and is mono. */
    static const uint8_t events[]={0,0x40,0,29,0,0x40,4,0,0,0x10,69,127,0x8c,1,0,69,1,0x60};
    uint8_t bank[1024];unsigned size=literal_score(bank,events,sizeof(events)),i,crossings=0;
    int16_t l,r,prior=0;int64_t power=0;
    fm1_doom_music_diagnostics d;
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    fm1_doom_music_set_volume(127);
    for(i=0;i<44100;i++){
        fm1_doom_music_sample_stereo(&l,&r);CHECK(r==l);power+=(int32_t)l*l;
        if(l>=0 && prior<0 && i>500)++crossings;prior=l;
    }
    /* The real two-operator patch includes strong harmonics; zero crossings
     * do not measure its fundamental as they did for the old triangle. */
    CHECK(power>1000000);CHECK(crossings>100);
    fm1_doom_music_pause(1);fm1_doom_music_get_diagnostics(&d);i=d.ticks;
    CHECK(!fm1_doom_music_sample());fm1_doom_music_get_diagnostics(&d);CHECK(d.ticks==i);
    fm1_doom_music_pause(0);fm1_doom_music_set_volume(0);CHECK(!fm1_doom_music_sample());
    fm1_doom_music_stop_locked();CHECK(!fm1_doom_music_is_playing());CHECK(!fm1_doom_music_sample());
    bank[17]=0;CHECK(fm1_doom_music_set_score(bank,size)<0); /* cycle */
    CHECK(fm1_doom_music_start(0)<0);CHECK(fm1_doom_music_set_score(0,size)<0);
    literal_score(bank,events,sizeof(events));bank[527]=0x50; /* unsupported event kind */
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));
    for(i=0;i<44100;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(d.errors && !fm1_doom_music_is_playing());
}

static void synth_tests(void)
{
    /* Eight full-velocity melodic VCOs: hold3s, release, allow3s to settle.
     * This drives the resonant VCF harder than E1M1's five held melodic keys. */
    static const uint8_t events[]={0,0x40,0,29,0,0x40,3,127,
        0,0x10,40,127,0,0x10,47,127,0,0x10,52,127,0,0x10,59,127,
        0,0x10,64,127,0,0x10,71,127,0,0x10,76,127,0,0x10,83,127,
        0xa4,3,0,40,0,0,47,0,0,52,0,0,59,0,0,64,0,0,71,0,0,76,0,0,83,
        0xa4,3,0x60};
    uint8_t bank[1024];unsigned size=literal_score(bank,events,sizeof(events)),i,nonzero=0;
    int sample;fm1_doom_music_diagnostics d;
    fm1_doom_music_set_synth_mode(1);CHECK(fm1_doom_music_get_synth_mode()==1);
    CHECK(!fm1_doom_music_set_score(bank,size));CHECK(!fm1_doom_music_start(0));fm1_doom_music_set_volume(127);
    for(i=0;i<44100u*6u;i++){
        sample=fm1_doom_music_sample();CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
        if(i>=44100u*5u)CHECK(sample==0); /* VCA release/VCF ring-down converge. */
    }
    fm1_doom_music_get_diagnostics(&d);CHECK(nonzero>44100 && !d.errors);
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));CHECK(!fm1_doom_music_start(1));
    for(i=0;i<44100;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(!d.errors);
    fm1_doom_music_toggle_synth_mode();CHECK(!fm1_doom_music_get_synth_mode());
    for(i=0;i<128;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_toggle_synth_mode();CHECK(fm1_doom_music_get_synth_mode()==1);
    fm1_doom_music_set_volume(0);for(i=0;i<1000;i++)CHECK(!fm1_doom_music_sample());
    fm1_doom_music_get_diagnostics(&d);CHECK(!d.errors && d.ticks>140);
    fm1_doom_music_stop_locked();fm1_doom_music_set_synth_mode(0);
}

int main(void)
{
    unsigned i,nonzero=0,peak=0;int32_t sample;uint64_t power=0;fm1_doom_music_diagnostics d;
    CHECK(fm1_doom_music_get_synth_mode()==0);
#ifdef FM1_TARGET_PI32V2
    module_tests();
#endif
    fm1_doom_music_set_synth_mode(0);
    synthetic_tests();
    synth_tests();
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));
    CHECK(!fm1_doom_music_start(1));
    for(i=0;i<44100u*97u;i++){
        sample=fm1_doom_music_sample();CHECK(sample>=-8192 && sample<=8191);nonzero+=sample!=0;
        power+=(int64_t)sample*sample;if((unsigned)abs(sample)>peak)peak=(unsigned)abs(sample);
    }
    fm1_doom_music_get_diagnostics(&d);
    printf("Measured original OPL2: peak=%u RMS=%.1f events=%lu loops=%lu errors=%lu\n",
           peak,sqrt((double)power/(44100u*97u)),(unsigned long)d.events,
           (unsigned long)d.loops,(unsigned long)d.errors);
    CHECK(nonzero>44100);CHECK(d.loops==1);CHECK(d.events>5824 && !d.errors);CHECK(d.active_voices<=9);
    CHECK(peak>300);CHECK(power/(44100u*97u)>10000u);
    printf("PASS original score loop: ticks=%lu events=%lu steals=%lu active=%u\n",
           (unsigned long)d.ticks,(unsigned long)d.events,(unsigned long)d.voice_steals,d.active_voices);
    printf("Music PCM16: peak=%u RMS=%.1f\n",peak,sqrt((double)power/(44100u*97u)));
    fm1_doom_music_stop_locked();CHECK(!fm1_doom_music_sample());
    CHECK(!fm1_doom_music_start(0));
    for(i=0;i<44100u*97u;i++)(void)fm1_doom_music_sample();
    fm1_doom_music_get_diagnostics(&d);CHECK(!fm1_doom_music_is_playing() && !d.errors);
    return 0;
}
