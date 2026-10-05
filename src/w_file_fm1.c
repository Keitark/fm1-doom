#include "fm1_doom_wad_file.h"
#include "i_system.h"
#include "w_file.h"

#include <string.h>

extern wad_file_class_t stdc_wad_file;

static const char *registered_path;
static fm1_fmd_t *registered_archive;
static wad_file_t archive_file;

static wad_file_t *archive_open(char *path)
{
    if (!registered_path || !registered_archive || strcmp(path, registered_path))
        return NULL;
    archive_file.length = registered_archive->wad_size;
    archive_file.mapped = NULL;
    return &archive_file;
}

static void archive_close(wad_file_t *wad)
{
    (void)wad;
}

static size_t archive_read(wad_file_t *wad, unsigned int offset,
                           void *buffer, size_t buffer_len)
{
    size_t expected, read;
    (void)wad;
    expected = offset < registered_archive->wad_size
        ? registered_archive->wad_size - offset : 0;
    if (expected > buffer_len) expected = buffer_len;
    read = fm1_fmd_read(registered_archive, offset, buffer, buffer_len);
    if (read != expected) I_Error("FMD1 WAD read failed at %u", offset);
    return read;
}

static wad_file_class_t archive_class = {
    archive_open, archive_close, archive_read
};

void fm1_doom_set_wad_archive(const char *path, fm1_fmd_t *archive)
{
    registered_path = path;
    registered_archive = archive;
    archive_file.file_class = &archive_class;
}

wad_file_t *W_OpenFile(char *path)
{
    wad_file_t *file = archive_class.OpenFile(path);
    return file ? file : stdc_wad_file.OpenFile(path);
}

void W_CloseFile(wad_file_t *wad)
{
    wad->file_class->CloseFile(wad);
}

size_t W_Read(wad_file_t *wad, unsigned int offset,
              void *buffer, size_t buffer_len)
{
    return wad->file_class->Read(wad, offset, buffer, buffer_len);
}
