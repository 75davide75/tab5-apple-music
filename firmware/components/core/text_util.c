#include "text_util.h"
#include <ctype.h>

bool url_encode(const char *src, char *dst, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        bool plain = isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = plain ? 1 : 3;
        if (o + need + 1 > cap) {
            if (cap) dst[0] = 0;
            return false;
        }
        if (plain) {
            dst[o++] = (char)*p;
        } else {
            dst[o++] = '%';
            dst[o++] = hex[*p >> 4];
            dst[o++] = hex[*p & 15];
        }
    }
    if (cap == 0) return false;
    dst[o] = 0;
    return true;
}
