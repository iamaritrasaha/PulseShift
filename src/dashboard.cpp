// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "dashboard.h"
#include <Arduino.h>

#if REVIX_OLED_ENABLED
#include <Wire.h>
namespace {
const char* statusName(ViewStatus s) {
  switch (s) {
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
}  // namespace
#endif

bool Dashboard::begin() {
#if REVIX_OLED_ENABLED
  Wire.begin(config::sda, config::scl);
  Wire.setTimeOut(20);
  ready_ = display_.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
#endif
  leds_.begin();
  // Master brightness scaling is computed in ShiftLightEngine using integer math.
  leds_.setBrightness(255);
  leds_.clear();
  leds_.show();
  // Start the boot animation clock after peripheral setup, not at reset time.
  shiftLight_.reset(millis());
#if REVIX_OLED_ENABLED
  return ready_;
#else
  return true;
#endif
}

void Dashboard::update(const RpmState& s, uint32_t now, bool healthy, bool configured, uint32_t timeout) {
  const auto status = viewStatus(s, now, timeout, healthy, configured);
  const bool live = (status == ViewStatus::Live);
  const uint16_t shownRpm = live ? s.rpm() : 0;

  // Process shift-light engine
  const bool ledsChanged = shiftLight_.update(now, shownRpm, status);
  if (ledsChanged) {
    const uint8_t count = shiftLight_.ledCount();
    for (uint8_t i = 0; i < count; ++i) {
      const RgbColor c = shiftLight_.getPixel(i);
      leds_.setPixelColor(i, leds_.Color(c.r, c.g, c.b));
    }
    leds_.show();
  }

#if REVIX_OLED_ENABLED
  if (!ready_) return;
  const bool alertActive = shiftLight_.isAlertActive();
  // Cap I2C traffic at 10 Hz even while RPM/animation changes, and avoid
  // redrawing an unchanged display once that interval has elapsed.
  const bool startupActive = live && !shiftLight_.isStartupComplete();
  if (drawn_ && (uint32_t(now - lastOledRefresh_) < 100 ||
      (lastRpm_ == shownRpm &&
      lastStatus_ == status &&
      lastId_ == s.lastId() &&
      lastAlertActive_ == alertActive && lastStartupActive_ == startupActive))) {
    return;
  }
  drawn_ = true;
  lastStartupActive_ = startupActive;
  lastOledRefresh_ = now;
  lastRpm_ = shownRpm;
  lastStatus_ = status;
  lastId_ = s.lastId();
  lastAlertActive_ = alertActive;

  display_.clearDisplay();
  display_.setTextColor(SSD1306_WHITE);
  display_.setTextSize(1);
  display_.setCursor(0, 0);
  display_.print("REVIX LINK");
  display_.setCursor(0, 10);
  display_.print(config::modeName);

  display_.setCursor(0, 22);
  display_.setTextSize(2);
  if (startupActive) {
    display_.print("STARTUP");
  } else if (alertActive) {
    display_.print("SHIFT!");
  } else if (live) {
    display_.print(shownRpm);
    display_.print(" RPM");
  } else {
    display_.print("-- RPM");
  }

  display_.setTextSize(1);
  display_.setCursor(0, 40);
  display_.print(statusName(status));
  display_.setCursor(0, 49);
  if (s.hasFrame()) {
    display_.print("ID: ");
    display_.print(s.lastId(), HEX);
  } else {
    display_.print("ID: --");
  }

  display_.drawRect(0, 58, 128, 6, SSD1306_WHITE);
  if (live && config::maxRpm > 0) {
    uint32_t barWidth = uint32_t(shownRpm) * 126 / config::maxRpm;
    if (barWidth > 126) barWidth = 126;
    display_.fillRect(1, 59, barWidth, 4, SSD1306_WHITE);
  }
  display_.display();
#endif
}
