// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "rpm_decoder.h"
#include "frame_log.h"
#include "dashboard.h"
#include "demo_commands.h"
#if REVIX_MODE != REVIX_BIKE
#include "simulated_ecu.h"
#endif
#if REVIX_MODE != REVIX_REPLAY
#include "can_transport.h"
#endif

namespace {
class Application {
 public:
  Application() : rpm_(mapping()) {}

  void begin() {
#if REVIX_SERIAL_DIAGNOSTICS
    Serial.begin(config::serialBaud);
    Serial.printf("REVIX LINK mode=%s SDK=%s\r\n", config::modeName, ESP.getSdkVersion());
#endif
#if REVIX_MODE == REVIX_BIKE
#if REVIX_SERIAL_DIAGNOSTICS
    Serial.printf("TARGET: %s / %s / %u WS2812B LEDs\r\n",
      config::vehicle, config::transceiver, config::ledCount);
    Serial.println("RPM mapping disabled; SN65HVD230 RS must be held HIGH for passive bench capture");
#endif
#endif
#if REVIX_MODE == REVIX_REPLAY
#if REVIX_SERIAL_DIAGNOSTICS
    Serial.println("SYNTHETIC FRAMES ONLY: does not validate TWAI or physical CAN");
    Serial.println("Type 'help' in Serial Monitor for interactive demo controls.");
#endif
#else
    transport_.begin();
#endif
    if (!dashboard_.begin()) {
#if REVIX_SERIAL_DIAGNOSTICS
      Serial.println("OLED INIT FAILED (CAN processing continues)");
#endif
    }
  }

  void tick() {
    const uint32_t now = millis();
#if REVIX_MODE == REVIX_REPLAY && REVIX_SERIAL_DIAGNOSTICS
    handleSerialInput();
#endif
#if REVIX_MODE != REVIX_REPLAY
    transport_.poll(now);
#endif
    CanFrame f;
    for (unsigned i = 0; i < 32; ++i) {
#if REVIX_MODE == REVIX_REPLAY
      const bool got = transport_.receive(now, f);
#else
      const bool got = transport_.receive(f, now);
#endif
      if (!got) break;
      const auto result = rpm_.ingest(f);
      logger_.push(f, result);
    }
#if REVIX_MODE == REVIX_TWAI_TEST
    // Consume completion after draining RX too; receive order is not assumed.
    transport_.poll(now);
    if (transport_.canSend() && uint32_t(now - lastSent_) >= config::framePeriodMs) {
      if (transport_.sendTest(demonstrationFrame(now, sequence_), now)) ++sequence_;
      lastSent_ = now;
    }
#endif
#if REVIX_MODE == REVIX_REPLAY
    constexpr bool healthy = true, configured = true;
#else
    const bool healthy = transport_.healthy(), configured = transport_.configured();
#endif
    logger_.pump();
    dashboard_.update(rpm_, now, healthy, configured);
#if REVIX_SERIAL_DIAGNOSTICS
    if (!finalReportPrinted_ && uint32_t(now - lastReport_) >= 1000) {
      lastReport_ = now;
      Serial.printf("APP mode=%s accepted=%lu rejected=%lu fresh=%d busFresh=%d rpm=%u alert=%d startup=%d logDropped=%lu\r\n",
        config::modeName, (unsigned long)rpm_.accepted(), (unsigned long)rpm_.rejected(),
        rpm_.fresh(now, config::staleMs) && healthy, rpm_.busFresh(now, config::staleMs), rpm_.rpm(),
        dashboard_.shiftLight().isAlertActive(), !dashboard_.shiftLight().isStartupComplete(),
        (unsigned long)logger_.dropped());
#if REVIX_MODE != REVIX_REPLAY
      transport_.diagnostics();
      // After a terminal fault, retain one final stopped-state report then
      // leave the terminal quiet. The dashboard still refreshes normally.
      if (!healthy) finalReportPrinted_ = true;
#endif
    }
#endif
  }

 private:
  static RpmMapping mapping() {
#if REVIX_MODE == REVIX_BIKE
    return RpmMapping{};  // Deliberately disabled: no guessed motorcycle mapping.
#else
    auto m = demonstrationMapping();
    m.maxRpm = config::maxRpm;
    return m;
#endif
  }

#if REVIX_MODE == REVIX_REPLAY && REVIX_SERIAL_DIAGNOSTICS
  void handleSerialInput() {
    // Bound serial work so a continuous input stream cannot starve CAN or LEDs.
    for (unsigned i = 0; i < 32 && Serial.available() > 0; ++i) {
      const auto result = command_.feed(static_cast<char>(Serial.read()));
      if (result == DemoCommandLine::Result::Ready) processCommand(command_.text());
      else if (result == DemoCommandLine::Result::Rejected)
        Serial.println("[ERROR] Command discarded: too long or invalid characters");
    }
  }

  void processCommand(char* cmd) {
    while (*cmd == ' ') ++cmd;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "help") == 0) {
      Serial.println("=== REVIX LINK DEMO COMMANDS ===");
      Serial.println("  help            : Show this help");
      Serial.println("  status          : Show shift-light status");
      Serial.println("  demo start      : Resume automated synthetic sweep");
      Serial.println("  demo stop       : Stop synthetic frame reception (RPM becomes stale)");
      Serial.println("  rpm <val>       : Inject synthetic RPM (e.g. rpm 4500)");
      Serial.println("  profile classic : Set classic style");
      Serial.println("  profile racing  : Set racing style");
      Serial.println("  profile minimal : Set minimal style");
      Serial.println("  brightness <val>: Set LED brightness (0..255)");
    } else if (strcmp(cmd, "status") == 0) {
      Serial.printf("[DEMO STATUS] RPM=%u Alert=%d Complete=%d Style=%d Brightness=%u Paused=%d Manual=%d\r\n",
        rpm_.rpm(),
        dashboard_.shiftLight().isAlertActive(),
        dashboard_.shiftLight().isStartupComplete(),
        static_cast<int>(dashboard_.shiftLight().style()),
        dashboard_.shiftLight().masterBrightness(),
        transport_.isPaused(),
        transport_.manualRpm());
    } else if (strcmp(cmd, "demo start") == 0) {
      transport_.setManualRpm(-1);
      transport_.setPaused(false);
      Serial.println("[DEMO] Automated synthetic cycle resumed.");
    } else if (strcmp(cmd, "demo stop") == 0) {
      transport_.setPaused(true);
      Serial.println("[DEMO] Synthetic reception stopped; RPM becomes stale after 1000 ms.");
    } else if (strncmp(cmd, "rpm ", 4) == 0) {
      uint32_t val = 0;
      if (parseDemoUnsigned(cmd + 4, 20000, val)) {
        transport_.setManualRpm(static_cast<int32_t>(val));
        transport_.setPaused(false);
        Serial.printf("[SYNTHETIC MANUAL RPM] %lu (simulation only, not real vehicle)\r\n", (unsigned long)val);
      } else {
        Serial.println("[ERROR] RPM must be 0..20000");
      }
    } else if (strncmp(cmd, "profile ", 8) == 0) {
      const char* p = cmd + 8;
      if (strcmp(p, "classic") == 0) {
        dashboard_.shiftLight().setStyle(ShiftLightStyle::Classic);
        Serial.println("[DEMO] Style set to CLASSIC.");
      } else if (strcmp(p, "racing") == 0) {
        dashboard_.shiftLight().setStyle(ShiftLightStyle::Racing);
        Serial.println("[DEMO] Style set to RACING.");
      } else if (strcmp(p, "minimal") == 0) {
        dashboard_.shiftLight().setStyle(ShiftLightStyle::Minimal);
        Serial.println("[DEMO] Style set to MINIMAL.");
      } else {
        Serial.println("[ERROR] Unknown style. Options: classic, racing, minimal");
      }
    } else if (strncmp(cmd, "brightness ", 11) == 0) {
      uint32_t b = 0;
      if (parseDemoUnsigned(cmd + 11, 255, b)) {
        dashboard_.shiftLight().setMasterBrightness(static_cast<uint8_t>(b));
        Serial.printf("[DEMO] Brightness set to %lu.\r\n", (unsigned long)b);
      } else {
        Serial.println("[ERROR] Brightness must be 0..255");
      }
    } else {
      Serial.println("[ERROR] Unknown command. Type 'help' for commands.");
    }
  }
#endif

  RpmState rpm_;
  Dashboard dashboard_;
  FrameLog logger_;
#if REVIX_MODE == REVIX_REPLAY
  ReplayTransport transport_;
#if REVIX_SERIAL_DIAGNOSTICS
  DemoCommandLine command_;
#endif
#else
  CanTransport transport_;
#endif
  uint32_t lastReport_ = 0;
  bool finalReportPrinted_ = false;
#if REVIX_MODE == REVIX_TWAI_TEST
  uint32_t lastSent_ = 0, sequence_ = 0;
#endif
};

Application app;
}  // namespace

void setup() { app.begin(); }
void loop() { app.tick(); }
