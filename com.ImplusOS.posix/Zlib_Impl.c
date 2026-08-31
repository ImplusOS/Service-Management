#include <Zlib.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

int zlib_compress(const uint8_t *in, size_t in_len, zlib_buffer_t *out) {
    if (!in || !out) return -1;

    uLongf destLen = compressBound((uLong)in_len);
    out->data = (uint8_t *)malloc(destLen);
    if (!out->data) return -2;

    int res = compress(out->data, &destLen, in, (uLong)in_len);
    if (res != Z_OK) {
        free(out->data);
        out->data = NULL;
        return -3;
    }

    out->size = (size_t)destLen;
    return 0;
}

int zlib_decompress(const uint8_t *in, size_t in_len, zlib_buffer_t *out) {
    if (!in || !out) return -1;
    
    uLongf destLen = (uLong)(in_len * 4); 
    out->data = (uint8_t *)malloc((size_t)destLen + 1u);
    if (!out->data) return -2;

    int res = uncompress(out->data, &destLen, in, (uLong)in_len);
    if (res != Z_OK) {
        free(out->data);
        out->data = NULL;
        return -3;
    }

    out->size = (size_t)destLen;
    out->data[out->size] = '\0';
    return 0;
}

void zlib_free_buffer(zlib_buffer_t *buf) {
    if (buf && buf->data) {
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
    }
}
