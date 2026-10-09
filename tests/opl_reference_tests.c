#include "fm1_doom_music.h"
#include "fm1_doom_opl.h"
#include "emu8950.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d frame%u: %s\n",__LINE__,frame,#x);exit(1);}}while(0)
extern const uint8_t fm1_doom_music_score[];
extern const uint32_t fm1_doom_music_score_len;
void opl_reference_init(unsigned volume);
void opl_reference_events(unsigned tick);
void opl_reference_volume(unsigned volume);
int16_t opl_reference_sample(void);
unsigned opl_reference_active(void);
OPL *reference_OPL_new(uint32_t clock,uint32_t rate);
void reference_OPL_delete(OPL *opl);
void reference_OPL_writeReg(OPL *opl,uint32_t reg,uint8_t value);
void reference_OPL_calc_buffer(OPL *opl,int16_t *buffer,uint32_t length);
static unsigned frame,actual_count,reference_count,total_writes;
static struct {unsigned reg,value;} actual[1024],reference[1024];

unsigned fm1_doom_sound_lock(void){return 0;}
void fm1_doom_sound_unlock(unsigned flags){(void)flags;}
int fm1_doom_sound_is_ready(void){return 1;}
void fm1_doom_opl_trace(unsigned reg,unsigned value)
{
    CHECK(actual_count<1024);actual[actual_count].reg=reg;actual[actual_count++].value=value;
}
void opl_reference_trace(unsigned reg,unsigned value)
{
    CHECK(reference_count<1024);reference[reference_count].reg=reg;reference[reference_count++].value=value;
}
static void compare_registers(void)
{
    CHECK(actual_count==reference_count);
    CHECK(!memcmp(actual,reference,actual_count*sizeof(actual[0])));
    total_writes+=actual_count;actual_count=reference_count=0;
    CHECK(fm1_doom_opl_active()==opl_reference_active());
}
static int16_t port_gain(int sample)
{
    sample*=4;
    return (int16_t)(sample>8191?8191:sample<-8192?-8192:sample);
}
static void native_noise_reference_test(void)
{
    static const uint8_t operators[]={0,1,2,8,9,10,16,17,18,3,4,5,11,12,13,19,20,21};
    static const uint32_t seeds[]={1u,0x800200u,0x7fffffu,0xffffffffu};
    OPL chip;OPL *reference_chip;unsigned seed,i,j;int16_t sample,want;
    for(seed=0;seed<sizeof(seeds)/sizeof(seeds[0]);seed++){
        OPL_init_static(&chip);reference_chip=reference_OPL_new(3579545,49716);CHECK(reference_chip);
        OPL_writeReg(&chip,1,0x20);reference_OPL_writeReg(reference_chip,1,0x20);
        for(i=0;i<sizeof(operators);i++){
            unsigned op=operators[i];
            static const uint8_t bases[]={0x20,0x40,0x60,0x80,0xe0};
            static const uint8_t values[]={0x21,0x08,0xf2,0x14,0};
            for(j=0;j<sizeof(bases);j++){
                OPL_writeReg(&chip,bases[j]+op,values[j]);
                reference_OPL_writeReg(reference_chip,bases[j]+op,values[j]);
            }
        }
        for(i=0;i<9;i++){
            OPL_writeReg(&chip,0xa0+i,0x98);reference_OPL_writeReg(reference_chip,0xa0+i,0x98);
            OPL_writeReg(&chip,0xb0+i,0x31);reference_OPL_writeReg(reference_chip,0xb0+i,0x31);
        }
        chip.noise=reference_chip->noise=seeds[seed];
        for(frame=0;frame<16384u;frame++){
            /* Preserve noise phase across normal FM, rhythm, then another
             * off/on transition at a non-block-aligned sample. */
            if(frame==4096u || frame==10003u || frame==12017u){
                uint8_t rhythm=frame==10003u?0:0x3f;
                OPL_writeReg(&chip,0xbd,rhythm);reference_OPL_writeReg(reference_chip,0xbd,rhythm);
            }
            OPL_calc_buffer(&chip,&sample,1);reference_OPL_calc_buffer(reference_chip,&want,1);
            CHECK(chip.noise==reference_chip->noise);CHECK(sample==want);
        }
        reference_OPL_delete(reference_chip);
    }
    printf("PASS native OPL rhythm toggles/noise phases: 65536 PCM samples identical\n");
}
static void mode_switch_reference_test(void)
{
    int16_t l,r,want;unsigned i,volume=64;
    CHECK(fm1_doom_music_get_synth_mode()==0); /* DOS timbre at boot. */
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));
    fm1_doom_music_set_volume(64);
    CHECK(!fm1_doom_music_start(0));opl_reference_init(64);compare_registers();
    for(frame=0;frame<44100u;frame++){
        if(frame==4410u)fm1_doom_music_set_synth_mode(1);
        if(frame==8820u)fm1_doom_music_set_synth_mode(0);
        if(frame==12600u || frame==13230u){
            volume=frame==12600u?0:64;
            fm1_doom_music_set_volume(volume);opl_reference_volume(volume);
        }
        if(frame==18900u){
            fm1_doom_music_pause(1);
            for(i=0;i<128;i++){
                fm1_doom_music_sample_stereo(&l,&r);CHECK(!l && !r);
                compare_registers(); /* A pause advances neither core/events. */
            }
            fm1_doom_music_pause(0);
        }
        if(frame%315u==0)opl_reference_events(frame/315u);
        fm1_doom_music_sample_stereo(&l,&r);compare_registers();
        want=port_gain(opl_reference_sample());CHECK(l==r);
        if(!volume)want=0; /* The port's explicit music mute is immediate. */
        /* Synth and crossfade may change PCM, but never OPL registers.
         * The first fully settled sample must recover the original stream,
         * even though synth rendering and the drum stem were skipped. */
        if(frame<4410u || frame>=8820u+63u)CHECK(l==want);
    }
    fm1_doom_music_stop_locked();
    actual_count=reference_count=total_writes=0;
    printf("PASS DOS fast path after synth toggle, mute and pause\n");
}
int main(void)
{
    int16_t l,r,want;unsigned volume=64;
    native_noise_reference_test();
    mode_switch_reference_test();
    fm1_doom_music_set_synth_mode(0);
    CHECK(!fm1_doom_music_set_score(fm1_doom_music_score,fm1_doom_music_score_len));
    CHECK(!fm1_doom_music_start(0));opl_reference_init(volume);compare_registers();
    for(frame=0;frame<44100u*96u;frame++){
        /* Runtime volume changes exercise exact DMX clipping and operator
         * volume writes without altering native MUS events or instruments. */
        if(frame==44100u || frame==88200u){volume=frame==44100u?127:64;fm1_doom_music_set_volume(volume);opl_reference_volume(volume);}
        if(frame%315u==0)opl_reference_events(frame/315u);
        fm1_doom_music_sample_stereo(&l,&r);compare_registers();
        want=port_gain(opl_reference_sample());CHECK(l==r);CHECK(l==want);
    }
    opl_reference_events(13440);(void)fm1_doom_music_sample();compare_registers();
    CHECK(!fm1_doom_music_is_playing());CHECK(total_writes>10000);
    printf("PASS full original MUS: %u ordered OPL register writes, %u PCM frames identical to unchanged driver/core\n",total_writes,frame);
    return 0;
}
