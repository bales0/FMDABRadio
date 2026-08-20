#ifndef FMDABRADIO_BACKLIGHT_H
#define FMDABRADIO_BACKLIGHT_H

#include <Arduino.h>

class Backlight {
 public:
  void begin(uint8_t pin, uint32_t now);
  void configure(uint8_t brightnessPercent, uint8_t dimPercent,
                 uint32_t dimAfterMs);
  void service(uint32_t now);

  void noteActivity(uint32_t now);

 private:
  static uint16_t dutyForPercent(uint8_t percent);
  void selectIdleTarget(uint32_t now);

  static constexpr uint8_t PWM_CHANNEL = 0;
  static constexpr uint8_t PWM_BITS = 10;
  static constexpr uint16_t PWM_MAX = (1U << PWM_BITS) - 1;
  static constexpr uint32_t PWM_FREQUENCY_HZ = 31250;
  static constexpr uint32_t DIM_AFTER_MS = 30000;
  static constexpr uint32_t FADE_TICK_MS = 10;
  static constexpr uint16_t FADE_STEP = 8;

  uint8_t _pin = 0xFF;
  uint16_t _currentDuty = PWM_MAX;
  uint16_t _targetDuty = PWM_MAX;
  uint32_t _lastActivityMs = 0;
  uint32_t _lastFadeMs = 0;
  uint32_t _dimAfterMs = DIM_AFTER_MS;
  uint8_t _brightnessPercent = 100;
  uint8_t _dimPercent = 20;
};

#endif
