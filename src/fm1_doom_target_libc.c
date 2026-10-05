/* Console diagnostics and explicit unsupported filesystem operations for the
 * FM-1 E1M1 profile. No save/load or shell command is exposed by this build. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

int fprintf(FILE *stream, const char *format, ...)
{
    char line[256];
    int count;
    va_list args;
    (void)stream;
    va_start(args, format);
    count = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (count < 0) return count;
    printf("%s", line);
    return count;
}

int fflush(FILE *stream) { (void)stream; return 0; }
int system(const char *command) { (void)command; errno = ENOSYS; return -1; }
int remove(const char *path) { (void)path; errno = ENOSYS; return -1; }
int rename(const char *old_path, const char *new_path)
{ (void)old_path; (void)new_path; errno = ENOSYS; return -1; }
int mkdir(const char *path, mode_t mode)
{ (void)path; (void)mode; errno = ENOSYS; return -1; }
