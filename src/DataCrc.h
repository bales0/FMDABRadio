// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "DataBytes.h"

namespace radio_data {
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
