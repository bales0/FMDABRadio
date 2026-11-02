#pragma once
#include "Arduino.h"
#include <deque>
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  unsigned clock,order,mode;
  SPISettings(unsigned c,unsigned o,unsigned m):clock(c),order(o),mode(m) {}
};
class HostSPI {
 public:
  std::vector<uint8_t> incoming,outgoing;
  std::deque<std::vector<uint8_t>> replies;
  std::vector<std::vector<uint8_t>> transactions;
  std::vector<uint8_t> current;
  void (*onTransfer)(size_t)=nullptr;
  size_t readAt=0;
  bool transaction=false;
  void beginTransaction(SPISettings settings) {
    assert(!transaction && hostPins[12]==HIGH && hostPins[13]==HIGH);
    assert(settings.mode==SPI_MODE0 && settings.order==MSBFIRST);
    hostEvents.push_back({1U,settings.clock,settings.mode});transaction=true;
    current.clear();
  }
  uint8_t transfer(uint8_t value) {
    assert(transaction && hostPins[12]==HIGH && hostPins[13]==LOW);
    if(current.empty() && value==0 && !replies.empty()) {
      incoming.assign(1,0);incoming.insert(incoming.end(),replies.front().begin(),replies.front().end());
      replies.pop_front();readAt=0;
    }
    current.push_back(value);
    if(onTransfer) onTransfer(current.size());
    outgoing.push_back(value);
    return readAt<incoming.size()?incoming[readAt++]:0;
  }
  void endTransaction() {
    assert(transaction && hostPins[12]==HIGH && hostPins[13]==HIGH);
    transaction=false;hostEvents.push_back({2U,0U,0U});
    transactions.push_back(current);
  }
  void reply(const std::vector<uint8_t>& value) {
    replies.clear();
    incoming.assign(1,0); // framing dummy byte
    incoming.insert(incoming.end(),value.begin(),value.end());
    readAt=0;outgoing.clear();
  }
  void script(std::initializer_list<std::vector<uint8_t>> values) {
    replies.assign(values.begin(),values.end());
  }
};
extern HostSPI SPI;
