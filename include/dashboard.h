// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "config.h"
#include "dashboard_model.h"
#include "shift_light.h"
#include <Adafruit_NeoPixel.h>
#if REVIX_OLED_ENABLED
#include <Adafruit_SSD1306.h>
#endif

class Dashboard {
 public:
  bool begin();
  void update(const RpmState& s, uint32_t now, bool healthy, bool configured);
  ShiftLightEngine& shiftLight() { return shiftLight_; }
  const ShiftLightEngine& shiftLight() const { return shiftLight_; }

 private:
#if REVIX_OLED_ENABLED
  Adafruit_SSD1306 display_{128, 64, &Wire, -1};
#endif
  Adafruit_NeoPixel leds_{config::ledCount, config::ledPin, NEO_GRB + NEO_KHZ800};
  ShiftLightEngine shiftLight_{config::makeShiftLightConfig()};
  bool ready_ = false;
  uint32_t lastOledRefresh_ = 0;
  uint16_t lastRpm_ = 0;
  ViewStatus lastStatus_ = ViewStatus::Fault;
  uint32_t lastId_ = 0;
  bool lastAlertActive_ = false;
  bool lastStartupActive_ = false, drawn_ = false;
};
