#ifndef FM1_DOOM_OPL_H
#define FM1_DOOM_OPL_H
#include <stddef.h>
#include <stdint.h>

/* Doom 1.9's nine-voice DOS OPL2 instrument/voice rules. Caller owns the
 * existing audio lock. The private FMGM bank contains original GENMIDI bytes. */
int fm1_doom_opl_set_bank(const uint8_t *bank, size_t length);
void fm1_doom_opl_start(unsigned volume);
void fm1_doom_opl_restart(void);
void fm1_doom_opl_stop(void);
void fm1_doom_opl_volume(unsigned volume);
int fm1_doom_opl_event(unsigned kind, unsigned channel, unsigned a, unsigned b);
int16_t fm1_doom_opl_sample(void);
/* Both outputs share native chip time. Drum output includes release tails
 * classified by the physical voice's last original GENMIDI instrument. */
void fm1_doom_opl_sample_split(int16_t *full, int16_t *drums);
unsigned fm1_doom_opl_active(void);
uint32_t fm1_doom_opl_steals(void);

#ifdef FM1_DOOM_OPL_TEST
/* Observe the actual hardware register writes for independent reference tests. */
void fm1_doom_opl_trace(unsigned reg, unsigned value);
#endif
#endif
