#ifndef FM1_DOOM_VOLUME_H
#define FM1_DOOM_VOLUME_H
#include <stdint.h>

/* Explicit diagnostic only. Peripheral registers are read once without
 * acknowledging completion or changing GPIO/ADC ownership. */
typedef struct {
    uint32_t adc_con, adc_res;
    uint32_t pb_dir, pb_die, pb_pu, pb_pd, pb_hd0, pb_hd1, pb_dieh;
    uint32_t wla_con0, pll_con1;
    uint32_t raw, accepted, target, gain, running, valid, waiting, samples, errors;
} fm1_doom_volume_hardware;

#endif
