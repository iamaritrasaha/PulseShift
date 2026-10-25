// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "config.h"
#include "dashboard_model.h"
#include <Adafruit_NeoPixel.h>
#if REVIX_OLED_ENABLED
#include <Adafruit_SSD1306.h>
#endif
class Dashboard {
 public:
  bool begin();
  void update(const RpmState&, uint32_t now, bool healthy, bool configured);
 private:
#if REVIX_OLED_ENABLED
  Adafruit_SSD1306 display_{128, 64, &Wire, -1};
#endif
  Adafruit_NeoPixel leds_{config::ledCount, config::ledPin, NEO_GRB + NEO_KHZ800};
  bool ready_ = false, drawn_ = false;
  uint32_t refreshed_ = 0, lastId_ = 0;
  uint16_t lastRpm_ = 0;
  ViewStatus lastStatus_ = ViewStatus::Fault;
};
