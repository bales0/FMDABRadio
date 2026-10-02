#pragma once

#include <Arduino.h>

// Fixed, non-blocking serial facade. Producers only enqueue bytes; service()
// moves a bounded chunk into the hardware UART when its TX FIFO has room.
// RX is delegated directly to the same HardwareSerial instance.
class NonBlockingSerialMonitor : public Stream {
 public:
  explicit NonBlockingSerialMonitor(HardwareSerial& serial)
      : _serial(serial), _head(0), _tail(0), _count(0), _dropped(0) {}

  void begin(unsigned long baud) { _serial.begin(baud); }

  size_t write(uint8_t value) override {
    if (_count >= kCapacity) {
      if (_dropped != 0xFFFFFFFFUL) ++_dropped;
      return 0;
    }
    _buffer[_head] = value;
    _head = (_head + 1U) % kCapacity;
    ++_count;
    return 1;
  }

  size_t write(const uint8_t* data, size_t length) override {
    if (data == nullptr) return 0;
    size_t accepted = 0;
    while (accepted < length && write(data[accepted]) == 1U) ++accepted;
    return accepted;
  }

  int available() override { return _serial.available(); }
  int read() override { return _serial.read(); }
  int peek() override { return _serial.peek(); }
  int availableForWrite() override {
    return static_cast<int>(kCapacity - _count);
  }
  void flush() override { service(kServiceBytes); }

  void service(size_t budget = kServiceBytes) {
    size_t room = static_cast<size_t>(_serial.availableForWrite());
    if (room > budget) room = budget;
    while (_count != 0U && room != 0U) {
      _serial.write(_buffer[_tail]);
      _tail = (_tail + 1U) % kCapacity;
      --_count;
      --room;
    }
  }

  uint32_t droppedBytes() const { return _dropped; }
  size_t queuedBytes() const { return _count; }

 private:
  static constexpr size_t kCapacity = 4096U;
  static constexpr size_t kServiceBytes = 64U;
  HardwareSerial& _serial;
  uint8_t _buffer[kCapacity];
  size_t _head;
  size_t _tail;
  size_t _count;
  uint32_t _dropped;
};
