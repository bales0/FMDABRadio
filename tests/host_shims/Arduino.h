// Host-only test double. Never part of the ESP32 build.
#pragma once
#include <algorithm>
#include <chrono>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
using std::min;
using std::max;
inline uint32_t millis() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void yield() {}
inline void delay(unsigned) {}
class Stream {
 public:
  void println(const char*) {}
};
struct HostESP { unsigned getFreeHeap() const { return 0U; } };
extern HostESP ESP;
