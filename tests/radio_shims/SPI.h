#pragma once
#include "Arduino.h"
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  unsigned clock,order,mode;
  SPISettings(unsigned c,unsigned o,unsigned m):clock(c),order(o),mode(m) {}
};
class HostSPI {
 public:
  std::vector<uint8_t> incoming,outgoing;
  size_t readAt=0;
  bool transaction=false;
  void beginTransaction(SPISettings settings) {
    assert(!transaction && hostPins[12]==HIGH && hostPins[13]==HIGH);
    assert(settings.mode==SPI_MODE0 && settings.order==MSBFIRST);
    hostEvents.push_back({1U,settings.clock,settings.mode});transaction=true;
  }
  uint8_t transfer(uint8_t value) {
    assert(transaction && hostPins[12]==HIGH && hostPins[13]==LOW);
    outgoing.push_back(value);
    return readAt<incoming.size()?incoming[readAt++]:0;
  }
  void endTransaction() {
    assert(transaction && hostPins[12]==HIGH && hostPins[13]==HIGH);
    transaction=false;hostEvents.push_back({2U,0U,0U});
  }
  void reply(const std::vector<uint8_t>& value) {
    incoming.assign(1,0); // framing dummy byte
    incoming.insert(incoming.end(),value.begin(),value.end());
    readAt=0;outgoing.clear();
  }
};
extern HostSPI SPI;
