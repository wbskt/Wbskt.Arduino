// The ESP32's SHA engine, for the real WebSockets library's handshake check.
#pragma once

#include <stddef.h>

enum esp_sha_type { SHA1 = 0 };

void esp_sha(esp_sha_type type, const unsigned char* input, size_t length, unsigned char* output);
