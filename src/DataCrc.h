// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "DataBytes.h"

namespace radio_data {
// MSC data-group CRC-16 (CCITT polynomial, complemented output).
inline uint16_t crc16(Bytes data, size_t length) {
  uint16_t crc=0xFFFFU;
  for(size_t index=0;index<length;++index) {
    crc^=uint16_t(data[index])<<8;
    for(unsigned bit=0;bit<8;++bit)
      crc=uint16_t((crc<<1)^((crc&0x8000U)?0x1021U:0U));
  }
  return crc^0xFFFFU;
}
// Reflected CRC-32/ISO-HDLC (PNG), table-free and reusable by future EPG.
inline uint32_t crc32(Bytes data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc ^ 0xFFFFFFFFU;
}
}  // namespace radio_data
