#include "base64.h"

const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64_encode(const uint8_t *data, size_t len) {
    String encoded;
    encoded.reserve(((len + 2) / 3) * 4);
    int pad = len % 3;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = 0;
        for (int j = 0; j < 3; ++j) {
            val <<= 8;
            if (i + j < len) {
                val |= data[i + j];
            }
        }
        encoded += b64_alphabet[(val >> 18) & 0x3F];
        encoded += b64_alphabet[(val >> 12) & 0x3F];
        encoded += (i + 1 < len) ? b64_alphabet[(val >> 6) & 0x3F] : '=';
        encoded += (i + 2 < len) ? b64_alphabet[val & 0x3F] : '=';
    }

    if (pad == 1) {
        encoded.setCharAt(encoded.length() - 1, '=');
    }
    return encoded;
}