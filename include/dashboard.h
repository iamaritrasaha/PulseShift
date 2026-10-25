#pragma once
#include "config.h"
#include "dashboard_model.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
class Dashboard {
 public:
  bool begin();
  void update(const RpmState&, uint32_t now, bool healthy, bool configured);
 private:
  Adafruit_SSD1306 display_{128, 64, &Wire, -1};
  Adafruit_NeoPixel leds_{config::ledCount, config::ledPin, NEO_GRB + NEO_KHZ800};
  bool ready_ = false, drawn_ = false;
  uint32_t refreshed_ = 0, lastId_ = 0;
  uint16_t lastRpm_ = 0;
  ViewStatus lastStatus_ = ViewStatus::Fault;
};
