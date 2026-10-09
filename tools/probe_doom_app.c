/* Offline link-size root only. app_main deliberately does not start Doom. */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"

const struct irq_info irq_info_table[] = {{-1, -1, -1}};
const struct task_info task_info_table[] = {
    {"app_core", 15, 4096, 1024},
    {"sys_event", 29, 512, 0},
    {"systimer", 14, 256, 0},
    {"sys_timer", 9, 512, 128},
    {0, 0, 0, 0, 0},
};

void app_main(void) { }
