/* Link-size probe only. Missing SDK libc functions are unavailable at runtime.
 * This file must not be used in a bootable game until each call site is adapted. */
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>

int fflush(FILE *stream) { (void)stream; return 0; }
int system(const char *command) { (void)command; return -1; }
int fprintf(FILE *stream, const char *format, ...)
{
    (void)stream; (void)format;
    return -1;
}
int remove(const char *path) { (void)path; return -1; }
int rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path; return -1;
}
int mkdir(const char *path, mode_t mode)
{
    (void)path; (void)mode; return -1;
}
