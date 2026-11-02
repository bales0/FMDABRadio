// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace radio_data {
// Small byte-addressable view, adapted from the reference epg::Bytes. Length
// remains an explicit argument at parser boundaries; no word-only IRAM access.
class Bytes {
 public:
  Bytes(const uint8_t* data = nullptr) : data_(data) {}
  explicit operator bool() const { return data_ != nullptr; }
  uint8_t operator[](size_t index) const { return data_[index]; }
  Bytes operator+(size_t offset) const { return Bytes(data_ + offset); }
  void copyTo(void* output, size_t length) const {
    memcpy(output, data_, length);
  }
  bool equals(Bytes other, size_t length) const {
    return memcmp(data_, other.data_, length) == 0;
  }
 private:
  const uint8_t* data_;
};
}  // namespace radio_data
