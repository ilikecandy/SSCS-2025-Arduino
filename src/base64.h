#ifndef BASE64_H
#define BASE64_H

#include <Arduino.h>

String base64_encode(const uint8_t *data, size_t len);

#endif // BASE64_H