// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "dashboard.h"
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE
#include "can_recorder.h"
#include "storage_manager.h"
#endif
#include <Arduino.h>

#if PULSESHIFT_OLED_ENABLED
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
#if PULSESHIFT_OLED_ENABLED
  Wire.begin(config::sda, config::scl);
  // Fix D6: 400 kHz fast mode drops transfer latency from ~95ms to ~24ms
  Wire.setClock(400000);
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
#if PULSESHIFT_OLED_ENABLED
  return ready_;
#else
  return true;
#endif
}

void Dashboard::renderFieldStatus(uint32_t now, bool healthy, bool configured) {
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE
  const uint8_t count = config::ledCount;
  if (count < 6) return;

  const uint8_t br = shiftLight_.masterBrightness();
  RgbColor pix[6]{};

  // Button press visual feedback takes priority across all LEDs
  if (now < feedbackUntilMs_) {
    const RgbColor white = scaleColor(RgbColor{255, 255, 255}, br);
    for (uint8_t i = 0; i < count; ++i) {
      leds_.setPixelColor(i, leds_.Color(white.r, white.g, white.b));
    }
    leds_.show();
    return;
  }

  // LED 0: Controller / System State
  if (!healthy) {
    // Red blink (100ms on, 100ms off) indicates fatal controller failure / bus-off
    const bool on = ((now / 100) % 2) == 0;
    pix[0] = on ? RgbColor{255, 0, 0} : RgbColor{0, 0, 0};
  } else if (!configured) {
    // Dim Yellow pulse if unconfigured
    const uint32_t phase = now % 2000;
    const uint8_t f = (phase < 1000) ? (phase * 100 / 1000 + 20) : ((2000 - phase) * 100 / 1000 + 20);
    pix[0] = scaleColor(RgbColor{255, 180, 0}, f);
  } else {
    // Healthy CAN controller:
    const bool active = (now - lastRxActivityMs_) < 1000;
    if (active) {
      // Receiving CAN traffic: Cyan/Green
      pix[0] = RgbColor{0, 220, 200};
    } else {
      // Waiting for CAN traffic: Gentle yellow breathing pulse (0.5 Hz)
      const uint32_t phase = now % 2000;
      const uint8_t f = (phase < 1000) ? (phase * 120 / 1000 + 25) : ((2000 - phase) * 120 / 1000 + 25);
      pix[0] = scaleColor(RgbColor{255, 200, 0}, f);
    }
  }

  // LED 1: RX Activity Heartbeat
  // Crisp Green pulse whenever CAN frames are actively arriving
  if ((now - lastRxActivityMs_) < 80) {
    pix[1] = RgbColor{0, 255, 0};
  } else {
    pix[1] = RgbColor{0, 0, 0};
  }

  // LEDs 2, 3, 4: Field Capture Stage Indication
  const auto stage = CanRecorder::instance().currentStage();
  if (CanRecorder::instance().isRecording()) {
    pix[2] = (stage >= CanRecorder::STAGE_KEY_ON_ENGINE_OFF) ? RgbColor{255, 160, 0} : RgbColor{0, 0, 0};
    pix[3] = (stage >= CanRecorder::STAGE_ENGINE_IDLING)     ? RgbColor{255, 160, 0} : RgbColor{0, 0, 0};
    pix[4] = (stage >= CanRecorder::STAGE_GENTLE_RPM)        ? RgbColor{255, 160, 0} : RgbColor{0, 0, 0};
  } else {
    // Paused: all stage LEDs slow blink Blue
    const bool on = ((now / 500) % 2) == 0;
    if (on) {
      pix[2] = pix[3] = pix[4] = RgbColor{0, 80, 255};
    }
  }

  // LED 5: Storage Status
  auto& sm = StorageManager::instance();
  if (!sm.isMounted() || sm.mountFailed()) {
    // Magenta blink (storage error)
    const bool on = ((now / 250) % 2) == 0;
    pix[5] = on ? RgbColor{255, 0, 255} : RgbColor{0, 0, 0};
  } else if (CanRecorder::instance().isStorageFull() || CanRecorder::instance().isFlashPreserved() || sm.isStorageFull()) {
    // Slow Amber blink (storage full or prior session capture preserved)
    const bool on = ((now / 500) % 2) == 0;
    pix[5] = on ? RgbColor{255, 140, 0} : RgbColor{0, 0, 0};
  } else {
    pix[5] = RgbColor{0, 0, 0}; // Storage OK
  }

  // Scale and send to hardware
  bool changed = false;
  for (uint8_t i = 0; i < count && i < 6; ++i) {
    const RgbColor scaled = scaleColor(pix[i], br);
    if (scaled != lastFieldPixels_[i]) {
      lastFieldPixels_[i] = scaled;
      changed = true;
    }
    leds_.setPixelColor(i, leds_.Color(scaled.r, scaled.g, scaled.b));
  }
  if (changed) {
    leds_.show();
  }
#else
  (void)now; (void)healthy; (void)configured;
#endif
}

void Dashboard::update(const RpmState& s, uint32_t now, bool healthy, bool configured, uint32_t timeout) {
  const auto status = viewStatus(s, now, timeout, healthy, configured);
  const bool live = (status == ViewStatus::Live);
  const uint16_t shownRpm = live ? s.rpm() : 0;

#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE
  // Test override allows bench testing shift-light LED patterns without physical CAN
  if (testRpmActive_) {
    const bool ledsChanged = shiftLight_.update(now, testRpm_, ViewStatus::Live);
    if (ledsChanged) {
      const uint8_t count = shiftLight_.ledCount();
      for (uint8_t i = 0; i < count; ++i) {
        const RgbColor c = shiftLight_.getPixel(i);
        leds_.setPixelColor(i, leds_.Color(c.r, c.g, c.b));
      }
      leds_.show();
    }
  } else if (!shiftLight_.isStartupComplete()) {
    const bool ledsChanged = shiftLight_.update(now, shownRpm, ViewStatus::Live);
    if (ledsChanged) {
      const uint8_t count = shiftLight_.ledCount();
      for (uint8_t i = 0; i < count; ++i) {
        const RgbColor c = shiftLight_.getPixel(i);
        leds_.setPixelColor(i, leds_.Color(c.r, c.g, c.b));
      }
      leds_.show();
    }
  } else {
    renderFieldStatus(now, healthy, configured);
  }
#else
  // Standard Shift-Light Operation for DEMO and VEHICLE modes
  const bool ledsChanged = shiftLight_.update(now, shownRpm, status);
  if (ledsChanged) {
    const uint8_t count = shiftLight_.ledCount();
    for (uint8_t i = 0; i < count; ++i) {
      const RgbColor c = shiftLight_.getPixel(i);
      leds_.setPixelColor(i, leds_.Color(c.r, c.g, c.b));
    }
    leds_.show();
  }
#endif

#if PULSESHIFT_OLED_ENABLED
  if (!ready_) return;
  const bool alertActive = shiftLight_.isAlertActive();
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
  display_.print("PulseShift");
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
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE
  } else {
    display_.setTextSize(1);
    display_.printf("STAGE %u\n", (unsigned)CanRecorder::instance().currentStage());
    display_.printf("RX:%lu REC:%lu\n", (unsigned long)s.accepted(),
                   (unsigned long)CanRecorder::instance().recordedFrames());
#else
  } else {
    display_.print("-- RPM");
#endif
  }

  display_.setTextSize(1);
  display_.setCursor(0, 40);
#if PULSESHIFT_OLED_ENABLED
  display_.print(statusName(status));
#endif
  display_.setCursor(0, 49);
  if (s.hasFrame()) {
    display_.print("ID: ");
    display_.print(s.lastId(), HEX);
  } else {
    display_.print("ID: --");
  }

  display_.drawRect(0, 58, 128, 6, SSD1306_WHITE);
  // Share the LED engine calibration: vehicle mode must never use demo bounds.
  const uint16_t maximum = shiftLight_.config().rpmMax;
  if (live && maximum > 0) {
    uint32_t barWidth = uint32_t(shownRpm) * 126 / maximum;
    if (barWidth > 126) barWidth = 126;
    display_.fillRect(1, 59, barWidth, 4, SSD1306_WHITE);
  }
  display_.display();
#endif
}
