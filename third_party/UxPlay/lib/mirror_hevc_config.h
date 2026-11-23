/* QiScreen: validate AirPlay's hvc1 parameter sets before reading or copying them. */
#ifndef MIRROR_HEVC_CONFIG_H
#define MIRROR_HEVC_CONFIG_H
#include <stddef.h>
#include <string.h>
static inline int mirror_hevc_config(const unsigned char *src, size_t size,
                                     unsigned char *dst, size_t capacity, size_t *written) {
    size_t offsets[3], lengths[3], pos = 0x75, total = 0;
    if (!written) return 0;
    *written = 0;
    if (!src || !dst || size < pos || memcmp(src + 4, "hvc1", 4)) return 0;
    for (size_t i = 0; i < 3; ++i) {
        if (pos > size || size - pos < 5 || src[pos] != 0xa0 + i ||
            src[pos+1] != 0 || src[pos+2] != 1) return 0;
        size_t length = ((size_t)src[pos+3] << 8) | src[pos+4];
        pos += 5;
        if (length < 2 || length > size - pos || ((src[pos] >> 1) & 0x3f) != 32 + i) return 0;
        if (total > capacity || capacity - total < 4 || length > capacity - total - 4) return 0;
        offsets[i] = pos; lengths[i] = length; pos += length; total += length + 4;
    }
    size_t out = 0;
    for (size_t i = 0; i < 3; ++i) {
        dst[out++] = 0; dst[out++] = 0; dst[out++] = 0; dst[out++] = 1;
        memcpy(dst + out, src + offsets[i], lengths[i]); out += lengths[i];
    }
    *written = out; return 1;
}
#endif
