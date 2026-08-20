/*
 * Non-blocking seven-button input for the FMDABRadio front panel.
 *
 * Debounce, long-press detection and repeat timing are driven by millis().
 * No callback performs radio, display or storage work.
 */
#ifndef FMDABRADIO_CONTROLS_H
#define FMDABRADIO_CONTROLS_H

#include <Arduino.h>

enum class ButtonId : uint8_t {
  VolumeUp,
  VolumeDown,
  Scan,
  Band,
  Select,
  ChannelDown,
  ChannelUp
};

enum class ButtonEventType : uint8_t {
  ShortPress,
  LongPress,
  Repeat
};

struct ButtonEvent {
  ButtonId button;
  ButtonEventType type;
};

class Controls {
 public:
  Controls();

  void begin(uint8_t volumeUpPin, uint8_t volumeDownPin, uint8_t scanPin,
             uint8_t bandPin, uint8_t selectPin, uint8_t channelDownPin,
             uint8_t channelUpPin);
  void poll(uint32_t nowMs);
  bool pop(ButtonEvent& event);
  uint32_t droppedEvents() const;

 private:
  struct ButtonState {
    uint8_t pin;
    ButtonId id;
    bool usePullup;
    bool deferShort;
    bool repeat;
    uint16_t longPressMs;
    bool rawPressed;
    bool stablePressed;
    bool longSent;
    uint32_t rawChangedMs;
    uint32_t pressedMs;
    uint32_t nextRepeatMs;
  };

  static constexpr uint8_t BUTTON_COUNT = 7;
  static constexpr uint8_t QUEUE_SIZE = 16;
  static constexpr uint16_t DEBOUNCE_MS = 25;
  static constexpr uint16_t REPEAT_START_MS = 400;
  static constexpr uint16_t REPEAT_SLOW_MS = 120;
  static constexpr uint16_t REPEAT_FAST_MS = 60;
  static constexpr uint16_t REPEAT_ACCELERATE_MS = 1500;

  void configure(uint8_t index, uint8_t pin, ButtonId id, bool usePullup,
                 bool deferShort, bool repeat, uint16_t longPressMs);
  void push(ButtonId button, ButtonEventType type);

  ButtonState _buttons[BUTTON_COUNT];
  ButtonEvent _queue[QUEUE_SIZE];
  uint8_t _head;
  uint8_t _tail;
  uint32_t _dropped;
};

#endif
