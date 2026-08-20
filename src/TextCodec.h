#pragma once

#include <Arduino.h>

// DAB Charset values registered by ETSI TS 101 756.
constexpr uint8_t DAB_CHARSET_EBU_LATIN = 0;
constexpr uint8_t DAB_CHARSET_UCS2_BE = 6;
constexpr uint8_t DAB_CHARSET_UTF8 = 15;

String decodeBroadcastText(const uint8_t* data, size_t length,
                           uint8_t charset);
String decodeRdsText(const uint8_t* data, size_t length);
const char* broadcastCharsetName(uint8_t charset);

