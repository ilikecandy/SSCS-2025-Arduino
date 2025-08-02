#ifndef BASE64_H
#define BASE64_H

#include <Arduino.h>

String base64_encode(const uint8_t *data, size_t len);
size_t base64_encode_to_buffer(const uint8_t *data, size_t len, char *buffer, size_t bufferSize);

#endif
