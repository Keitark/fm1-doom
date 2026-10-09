/* Doomgeneric platform interface shared by the host rig and future FM-1 task. */
#include "fm1_doom_runtime.h"
#include "doomgeneric.h"
#include "m_argv.h"

static fm1_doom_port port;
static int bound;
pixel_t *DG_ScreenBuffer;
int usemouse;

void M_FindResponseFile(void);
void D_DoomMain(void);

int fm1_doom_bind_io(const fm1_doom_io *io)
{
    if (bound || fm1_doom_port_init(&port, io)) return -1;
    bound = 1;
    return 0;
}

fm1_doom_port *fm1_doom_active_port(void) { return bound ? &port : 0; }

void doomgeneric_Create(int argc, char **argv)
{
    myargc = argc;
    myargv = argv;
    M_FindResponseFile();
    DG_Init();
    D_DoomMain();
}

void DG_Init(void) { if (!bound) abort(); }
void DG_DrawFrame(void) { }
void DG_SleepMs(uint32_t ms) { if (bound) port.io.sleep_ms(port.io.context, ms); }
uint32_t DG_GetTicksMs(void) { return bound ? port.io.ticks_ms(port.io.context) : 0; }
int DG_GetKey(int *pressed, unsigned char *key)
{
    return bound ? fm1_doom_next_key(&port, pressed, key) : 0;
}
void DG_SetWindowTitle(const char *title) { (void)title; }
