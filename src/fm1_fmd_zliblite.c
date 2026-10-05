#include "fm1_doom_archive.h"
#include "net/zliblite/zlib.h"

/* FM-1 SDK binding for independently compressed FMD1 blocks. The decoder's
 * heap and link cost still require a full firmware build and measurement. */
int fm1_fmd_zliblite_inflate(void *context, const uint8_t *src, size_t src_len,
                             uint8_t *dst, size_t dst_len)
{
    uLongf actual = (uLongf)dst_len;
    (void)context;
    if ((size_t)(uLong)src_len != src_len ||
        (size_t)(uLong)dst_len != dst_len) {
        return -1;
    }
    return uncompress(dst, &actual, src, (uLong)src_len) == Z_OK
        && actual == (uLongf)dst_len ? 0 : -1;
}
