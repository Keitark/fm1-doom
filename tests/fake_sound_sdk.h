#ifndef FM1_FAKE_SOUND_SDK_H
#define FM1_FAKE_SOUND_SDK_H
#include <stdint.h>
typedef uint8_t u8;
typedef unsigned spinlock_t;
#define ___interrupt
#define IIS_PORTC 1
#define IRQ_ALNK_IDX 11
struct iis_platform_data {
    u8 channel_in,channel_out,port_sel,data_width,mclk_output,slave_mode,
       update_edge,f32e,keep_alive,sel,width_16_to_24;
    uint16_t dump_points_num,sr_points;
};
unsigned fm1_sound_test_irq_save(void);
void fm1_sound_test_irq_restore(unsigned);
void fm1_sound_test_lock(spinlock_t *);
void fm1_sound_test_unlock(spinlock_t *);
uint32_t fm1_sound_test_hardware_read(uint32_t);
#define local_irq_save(flags) ((flags) = fm1_sound_test_irq_save())
#define local_irq_restore(flags) fm1_sound_test_irq_restore(flags)
#define arch_spin_lock(lock) fm1_sound_test_lock(lock)
#define arch_spin_unlock(lock) fm1_sound_test_unlock(lock)
int iis_open(struct iis_platform_data *,u8);
void iis_close(u8);
int iis_set_sample_rate(int,u8);
void iis_set_dec_data_handler(void *,void (*)(void *,u8 *,int,u8),u8);
void iis_channel_on(u8,u8);
void iis_channel_off(u8,u8);
void iis_irq_handler(u8);
unsigned long jiffies_half_msec(void);
void request_irq(unsigned,int,void (*)(void),unsigned);
void bit_clr_ie(unsigned,unsigned);
void unrequest_irq(unsigned,unsigned);
#endif
