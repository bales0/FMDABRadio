#include "Controls.h"

namespace {

bool timeReached(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

}  // namespace

Controls::Controls() : _head(0), _tail(0), _dropped(0) {
  memset(_buttons, 0, sizeof(_buttons));
  memset(_queue, 0, sizeof(_queue));
}

void Controls::begin(uint8_t volumeUpPin, uint8_t volumeDownPin, uint8_t scanPin,
                     uint8_t bandPin, uint8_t selectPin,
                     uint8_t channelDownPin, uint8_t channelUpPin) {
  configure(0, volumeUpPin, ButtonId::VolumeUp, true, false, true, 0);
  configure(1, volumeDownPin, ButtonId::VolumeDown, true, false, true, 0);
  configure(2, scanPin, ButtonId::Scan, false, true, false, 1000);
  configure(3, bandPin, ButtonId::Band, false, false, false, 0);
  configure(4, selectPin, ButtonId::Select, true, true, false, 800);
  configure(5, channelDownPin, ButtonId::ChannelDown, true, false, true, 0);
  configure(6, channelUpPin, ButtonId::ChannelUp, true, false, true, 0);
}

void Controls::poll(uint32_t nowMs) {
  for (uint8_t i = 0; i < BUTTON_COUNT; ++i) {
    ButtonState& button = _buttons[i];
    const bool rawPressed = digitalRead(button.pin) == LOW;

    if (rawPressed != button.rawPressed) {
      button.rawPressed = rawPressed;
      button.rawChangedMs = nowMs;
    }

    if (button.stablePressed != button.rawPressed &&
        static_cast<uint32_t>(nowMs - button.rawChangedMs) >= DEBOUNCE_MS) {
      button.stablePressed = button.rawPressed;
      if (button.stablePressed) {
        button.pressedMs = nowMs;
        button.nextRepeatMs = nowMs + REPEAT_START_MS;
        button.longSent = false;
        if (!button.deferShort) {
          push(button.id, ButtonEventType::ShortPress);
        }
      } else if (button.deferShort && !button.longSent) {
        push(button.id, ButtonEventType::ShortPress);
      }
    }

    if (!button.stablePressed) continue;
    const uint32_t heldMs = nowMs - button.pressedMs;

    if (button.longPressMs && !button.longSent &&
        heldMs >= button.longPressMs) {
      button.longSent = true;
      push(button.id, ButtonEventType::LongPress);
    }

    if (button.repeat && timeReached(nowMs, button.nextRepeatMs)) {
      push(button.id, ButtonEventType::Repeat);
      button.nextRepeatMs =
          nowMs + (heldMs >= REPEAT_ACCELERATE_MS ? REPEAT_FAST_MS
                                                  : REPEAT_SLOW_MS);
    }
  }
}

bool Controls::pop(ButtonEvent& event) {
  if (_head == _tail) return false;
  event = _queue[_tail];
  _tail = static_cast<uint8_t>((_tail + 1) % QUEUE_SIZE);
  return true;
}

uint32_t Controls::droppedEvents() const {
  return _dropped;
}

void Controls::configure(uint8_t index, uint8_t pin, ButtonId id,
                         bool usePullup, bool deferShort, bool repeat,
                         uint16_t longPressMs) {
  ButtonState& button = _buttons[index];
  button.pin = pin;
  button.id = id;
  button.usePullup = usePullup;
  button.deferShort = deferShort;
  button.repeat = repeat;
  button.longPressMs = longPressMs;
  pinMode(pin, usePullup ? INPUT_PULLUP : INPUT);
  button.rawPressed = digitalRead(pin) == LOW;
  button.stablePressed = button.rawPressed;
  button.longSent = false;
  button.rawChangedMs = millis();
  button.pressedMs = button.rawChangedMs;
  button.nextRepeatMs = button.rawChangedMs + REPEAT_START_MS;
}

void Controls::push(ButtonId button, ButtonEventType type) {
  const uint8_t next = static_cast<uint8_t>((_head + 1) % QUEUE_SIZE);
  if (next == _tail) {
    ++_dropped;
    return;
  }
  _queue[_head] = {button, type};
  _head = next;
}
