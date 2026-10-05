#include "fm1_doom_music.h"
#include "fm1_doom_opl.h"
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
int main(void)
{
    int16_t l,r,want;unsigned volume=64;
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
