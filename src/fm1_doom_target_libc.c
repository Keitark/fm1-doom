/* The pinned SDK has no console transport. Engine fatal errors use the RAM
 * diagnostic hook; discard other console output without formatting it. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The SDK mounts SDFILE for configuration; this target has no FAT volume.
 * VFS references this FAT-only directory extension even when FAT is unused.
 * Returning unsupported here keeps the FAT drivers out of the application. */
struct vfscan;
int __fat_fscan_subpath_or_pre_dir(struct vfscan *scan, char *path,
                                  unsigned char selected, unsigned depth)
{
    (void)scan; (void)path; (void)selected; (void)depth;
    return -ENOSYS;
}

/* The SDK's shared string archive also pulls atof and software double math.
 * Doom needs only this bounded allocation-and-copy operation from that member. */
char *strdup(const char *source)
{
    size_t length = strlen(source) + 1u;
    char *copy = malloc(length);
    if (copy) memcpy(copy, source, length);
    return copy;
}

int fprintf(FILE *stream, const char *format, ...)
{
    (void)stream;
    (void)format;
    return 0;
}

int fflush(FILE *stream) { (void)stream; return 0; }
int system(const char *command) { (void)command; errno = ENOSYS; return -1; }
int remove(const char *path) { (void)path; errno = ENOSYS; return -1; }
int rename(const char *old_path, const char *new_path)
{ (void)old_path; (void)new_path; errno = ENOSYS; return -1; }
int mkdir(const char *path, mode_t mode)
{ (void)path; (void)mode; errno = ENOSYS; return -1; }
