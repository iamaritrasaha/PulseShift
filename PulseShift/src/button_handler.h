// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include <stdint.h>
#include <Arduino.h>

class ButtonHandler {
 public:
  static constexpr uint8_t BUTTON_PIN = 0; // BOOT button on ESP32 DevKit V1
  static constexpr uint32_t DEBOUNCE_MS = 50;
  static constexpr uint32_t LONG_PRESS_MS = 2500;

  enum class Event {
    None,
    ShortPress,
    LongPress
  };

  ButtonHandler() = default;

  void begin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    lastState_ = digitalRead(BUTTON_PIN);
    lastChangeMs_ = millis();
    longPressTriggered_ = false;
  }

  Event poll(uint32_t now) {
    const int raw = digitalRead(BUTTON_PIN);
    Event evt = Event::None;

    if (raw != lastRaw_) {
      lastRaw_ = raw;
      lastChangeMs_ = now;
    }

    if (uint32_t(now - lastChangeMs_) >= DEBOUNCE_MS) {
      if (raw != debouncedState_) {
        debouncedState_ = raw;
        if (debouncedState_ == LOW) {
          // Button pressed down
          pressStartMs_ = now;
          longPressTriggered_ = false;
        } else {
          // Button released (went HIGH)
          if (!longPressTriggered_) {
            const uint32_t duration = now - pressStartMs_;
            if (duration >= DEBOUNCE_MS && duration < LONG_PRESS_MS) {
              evt = Event::ShortPress;
            }
          }
        }
      } else if (debouncedState_ == LOW && !longPressTriggered_) {
        // Button held down continuously
        if (uint32_t(now - pressStartMs_) >= LONG_PRESS_MS) {
          longPressTriggered_ = true;
          evt = Event::LongPress;
        }
      }
    }

    lastState_ = raw;
    return evt;
  }

  bool isPressed() const { return debouncedState_ == LOW; }

 private:
  int lastRaw_ = HIGH;
  int debouncedState_ = HIGH;
  int lastState_ = HIGH;
  uint32_t lastChangeMs_ = 0;
  uint32_t pressStartMs_ = 0;
  bool longPressTriggered_ = false;
};
