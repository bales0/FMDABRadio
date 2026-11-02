// Host-only radio hardware facade with deterministic time and GPIO recording.
#pragma once
#include <algorithm>
#include <cassert>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using std::min;
using std::max;
#define IRAM_ATTR
#define PROGMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 3
extern uint32_t hostMillis;
extern int hostPins[256];
struct HostEvent { unsigned kind,pin,value; };
extern std::vector<HostEvent> hostEvents;
inline uint32_t millis() { return hostMillis; }
inline uint32_t micros() { return hostMillis*1000U; }
inline void yield() {}
inline void digitalWrite(uint8_t pin,int value) {
  hostPins[pin]=value;hostEvents.push_back({0U,pin,unsigned(value)});
}
inline int digitalRead(uint8_t pin) { return hostPins[pin]; }
inline void pinMode(uint8_t,int) {}
inline unsigned digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(unsigned,void(*)(),int) {}
inline void noInterrupts() {}
inline void interrupts() {}
inline uint8_t pgm_read_byte(const uint8_t* pointer) { return *pointer; }
class Stream { public: void println(const char*) {} };
struct HostESP { unsigned getFreeHeap() const { return 0; } };
extern HostESP ESP;
