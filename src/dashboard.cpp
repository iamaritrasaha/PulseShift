// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "dashboard.h"
#include <Wire.h>
namespace {
const char* statusName(ViewStatus s) {
  switch(s) {
    case ViewStatus::Fault: return "CAN HALTED / ERROR";
    case ViewStatus::Unconfigured: return "SET VERIFIED BITRATE";
    case ViewStatus::Disconnected: return "CAN DISCONNECTED";
    case ViewStatus::Unmapped: return "CAN RX / RPM UNMAPPED";
    case ViewStatus::Invalid: return "CAN RX / INVALID RPM";
    case ViewStatus::Stale: return "RPM STALE";
    case ViewStatus::Live: return "CAN FRAME VALID";
  }
  return "UNKNOWN";
}
}
bool Dashboard::begin() {
  Wire.begin(config::sda, config::scl);
  Wire.setTimeOut(20);
  ready_ = display_.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  leds_.begin(); leds_.setBrightness(config::ledBrightness); leds_.clear(); leds_.show();
  return ready_;
}
void Dashboard::update(const RpmState& s, uint32_t now, bool healthy, bool configured) {
  if (drawn_ && uint32_t(now-refreshed_) < 100) return;
  refreshed_ = now;
  const auto status = viewStatus(s, now, config::staleMs, healthy, configured);
  const bool live = status == ViewStatus::Live;
  const uint16_t shownRpm = live ? s.rpm() : 0;
  if (drawn_ && lastRpm_ == shownRpm && lastStatus_ == status && lastId_ == s.lastId()) return;
  drawn_ = true; lastRpm_ = shownRpm; lastStatus_ = status; lastId_ = s.lastId();
  leds_.clear();
  const auto count = litLedCount(shownRpm, config::maxRpm, config::ledCount, live);
  for (uint8_t i=0; i<count; ++i) leds_.setPixelColor(i,
    i<2 ? leds_.Color(0,255,0) : i<4 ? leds_.Color(255,160,0) : leds_.Color(255,0,0));
  leds_.show();
  if (!ready_) return;
  display_.clearDisplay(); display_.setTextColor(SSD1306_WHITE); display_.setTextSize(1);
  display_.setCursor(0,0); display_.print("REVIX LINK");
  display_.setCursor(0,10); display_.print(config::modeName);
  display_.setCursor(0,22); display_.setTextSize(2);
  if (live) { display_.print(shownRpm); display_.print(" RPM"); } else display_.print("-- RPM");
  display_.setTextSize(1); display_.setCursor(0,40); display_.print(statusName(status));
  display_.setCursor(0,49);
  if (s.hasFrame()) { display_.print("ID: "); display_.print(s.lastId(), HEX); }
  else display_.print("ID: --");
  display_.drawRect(0,58,128,6,SSD1306_WHITE);
  if (live) display_.fillRect(1,59,uint32_t(shownRpm)*126/config::maxRpm,4,SSD1306_WHITE);
  display_.display();
}
