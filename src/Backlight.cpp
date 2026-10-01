#include "Backlight.h"

void Backlight::begin(uint8_t pin, uint32_t now) {
  _pin = pin;
  _lastActivityMs = now;
  _lastFadeMs = now;
  // Keep the panel dark until setup has drawn a complete, valid first frame
  // and loaded the persisted brightness. This also avoids a white flash while
  // the ST7735 controller is still in reset/initialisation.
  _currentDuty = 0;
  _targetDuty = 0;
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_BITS);
  ledcAttachPin(_pin, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, _currentDuty);
}

void Backlight::configure(uint8_t brightnessPercent, uint8_t dimPercent,
                          uint32_t dimAfterMs) {
  _brightnessPercent = constrain(brightnessPercent, 20, 100);
  _dimPercent = constrain(dimPercent, 5, _brightnessPercent);
  _dimAfterMs = dimAfterMs < 5000 ? 5000 : dimAfterMs;
  _targetDuty = dutyForPercent(_brightnessPercent);
}

void Backlight::service(uint32_t now) {
  if (_pin == 0xFF) return;
  selectIdleTarget(now);
  if (static_cast<uint32_t>(now - _lastFadeMs) < FADE_TICK_MS) return;
  _lastFadeMs = now;

  if (_currentDuty < _targetDuty) {
    const uint16_t remaining = _targetDuty - _currentDuty;
    _currentDuty += remaining < FADE_STEP ? remaining : FADE_STEP;
  } else if (_currentDuty > _targetDuty) {
    const uint16_t remaining = _currentDuty - _targetDuty;
    _currentDuty -= remaining < FADE_STEP ? remaining : FADE_STEP;
  }
  ledcWrite(PWM_CHANNEL, _currentDuty);
}

void Backlight::noteActivity(uint32_t now) {
  _lastActivityMs = now;
  _targetDuty = dutyForPercent(_brightnessPercent);
}

uint16_t Backlight::dutyForPercent(uint8_t percent) {
  if (percent >= 100) return PWM_MAX;
  const uint32_t squared = static_cast<uint32_t>(percent) * percent;
  return static_cast<uint16_t>((PWM_MAX * squared + 5000) / 10000);
}

void Backlight::selectIdleTarget(uint32_t now) {
  const uint32_t idleMs = now - _lastActivityMs;
  if (idleMs >= _dimAfterMs) {
    _targetDuty = dutyForPercent(_dimPercent);
  } else {
    _targetDuty = dutyForPercent(_brightnessPercent);
  }
}
