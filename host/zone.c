#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char *I_ZoneBase_Original(int *size);

/* Host-only diagnostic: let a staged WAD run with a sub-MiB Doom zone. */
unsigned char *I_ZoneBase(int *size)
{
    const char *requested = getenv("FM1_DOOM_ZONE_KIB");
    char *end;
    long kib;
    unsigned char *zone;
    if (!requested || !*requested)
        return I_ZoneBase_Original(size);
    kib = strtol(requested, &end, 10);
    if (*end || kib < 64 || kib > 4096) {
        fprintf(stderr, "FM1_DOOM_ZONE_KIB must be 64..4096\n");
        exit(2);
    }
    *size = (int)kib * 1024;
    zone = malloc((size_t)*size);
    if (!zone) {
        fprintf(stderr, "Cannot allocate %ld KiB Doom zone\n", kib);
        exit(2);
    }
    printf("Diagnostic Doom zone: %ld KiB\n", kib);
    return zone;
}
