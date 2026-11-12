// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "rpm_decoder.h"
#include "frame_log.h"
#include "dashboard.h"
#include "storage_manager.h"
#include "diag_logger.h"
#include "can_recorder.h"
#include "button_handler.h"
#include "serial_protocol.h"

#if PULSESHIFT_MODE == PULSESHIFT_REPLAY || PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST
#include "simulated_ecu.h"
#endif
#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
#include "can_transport.h"
#endif

namespace {
class Application {
 public:
  Application() : rpm_(initialState()) {}

  void begin() {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.begin(config::serialBaud);
    Serial.printf("PulseShift mode=%s SDK=%s\r\n", config::modeName, ESP.getSdkVersion());
#endif

    // Initialize non-volatile persistent storage (LittleFS & Preferences NVS)
    auto& sm = StorageManager::instance();
    const bool fsOk = sm.begin(false); // Non-destructive mount

    // Initialize diagnostic logger
    auto& diag = DiagLogger::instance();
    diag.begin(sm.bootCount(), sm.currentSessionId());
    diag.logBoot(config::modeName, rpm_.mapped());
    diag.logStorageState(fsOk, sm.usedBytes(), sm.freeBytes(), sm.isStorageFull());

    // Initialize raw CAN recorder
#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
    const uint32_t bitrate = receiveBitrate();
#else
    const uint32_t bitrate = 0;
#endif
    CanRecorder::instance().begin(sm.currentSessionId(), sm.bootCount(), bitrate, config::modeName, config::vehicle);

    // Initialize BOOT button handler (GPIO0)
    button_.begin();

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    if (!fsOk) {
      Serial.println("[STORAGE WARNING] LittleFS mount failed; offline logging disabled until formatted.");
    } else {
      Serial.printf("[STORAGE] LittleFS mounted: %lu / %lu bytes used (%lu free)\r\n",
                    (unsigned long)sm.usedBytes(), (unsigned long)sm.totalBytes(), (unsigned long)sm.freeBytes());
    }
#endif

#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("TARGET: %s / %s / %u WS2812B LEDs\r\n",
      config::vehicle, config::transceiver, config::ledCount);
    Serial.println("RECEIVE ONLY: actual module RS-high/TXD bias must be electrically verified");
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    Serial.printf("PROFILE id=%s status=%s ready=%d\r\n", selectedVehicleProfile().identifier,
      profileStatusName(selectedVehicleProfile().status), rpm_.mapped());
#else
    Serial.println("CAN_CAPTURE: raw frames and offline diagnostics active");
#endif
#endif
#endif

#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.println("SYNTHETIC FRAMES ONLY: does not validate TWAI or physical CAN");
    Serial.println("Type 'help' in Serial Monitor for interactive demo and diagnostic controls.");
#endif
#else
    transport_.begin();
#endif

    if (!dashboard_.begin()) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.println("OLED INIT FAILED (CAN processing continues)");
#endif
    }
  }

  void tick() {
    const uint32_t now = millis();

    // 1. Process physical BOOT button events
    handleButton(now);

    // 2. Process serial commands (STATUS, LOGS, DUMP, CLEAR, etc.)
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    handleSerialInput();
#endif

    // 3. Poll hardware transport
#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
    transport_.poll(now);
#endif

    // 4. Ingest incoming CAN frames
    CanFrame f;
    for (unsigned i = 0; i < 32; ++i) {
#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
      const bool got = transport_.receive(now, f);
#else
      const bool got = transport_.receive(f, now);
#endif
      if (!got) break;

      dashboard_.notifyRxActivity(now);
      const auto result = rpm_.ingest(f);
      logger_.push(f, result);

#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
      CanRecorder::instance().push(f);
#endif
    }

#if PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST
    if (transport_.canSend() && uint32_t(now - lastSent_) >= config::framePeriodMs) {
      if (transport_.sendTest(demonstrationFrame(now, sequence_), now)) ++sequence_;
      lastSent_ = now;
    }
#endif

#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
    constexpr bool healthy = true, configured = true;
#else
    const bool healthy = transport_.healthy();
    const bool configured = transport_.configured()
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
      && rpm_.mapped()
#endif
      ;
#endif

    // 5. Service storage queues
    CanRecorder::instance().pump(now);

    // 6. Evaluate stale timeouts and refresh UI
    rpm_.expire(now, rpm_.timeoutMs(config::staleMs), config::staleMs);
    logger_.pump();
    dashboard_.update(rpm_, now, healthy, configured, rpm_.timeoutMs(config::staleMs));

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    // 7. Periodic telemetry output
    if (!finalReportPrinted_ && uint32_t(now - lastReport_) >= 1000) {
      lastReport_ = now;
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
      logger_.status("APP mode=%s accepted=%lu rejected=%lu recFrames=%lu recDropped=%lu stage=%u",
        config::modeName, (unsigned long)rpm_.accepted(), (unsigned long)rpm_.rejected(),
        (unsigned long)CanRecorder::instance().recordedFrames(),
        (unsigned long)CanRecorder::instance().droppedFrames(),
        (unsigned)CanRecorder::instance().currentStage());
      logger_.status("APP fresh=%d busFresh=%d rpm=%u configured=%d healthy=%d",
        rpm_.fresh(now, rpm_.timeoutMs(config::staleMs)) && healthy,
        rpm_.busFresh(now, config::staleMs), rpm_.rpm(), configured, healthy);
#else
      Serial.printf("APP mode=%s accepted=%lu rejected=%lu fresh=%d busFresh=%d rpm=%u alert=%d startup=%d logDropped=%lu\r\n",
        config::modeName, (unsigned long)rpm_.accepted(), (unsigned long)rpm_.rejected(),
        rpm_.fresh(now, rpm_.timeoutMs(config::staleMs)) && healthy, rpm_.busFresh(now, config::staleMs), rpm_.rpm(),
        dashboard_.shiftLight().isAlertActive(), !dashboard_.shiftLight().isStartupComplete(),
        (unsigned long)logger_.dropped());
#endif
#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
      transport_.diagnostics();
      if (!healthy) finalReportPrinted_ = true;
#endif
    }
#endif
  }

 private:
  void handleButton(uint32_t now) {
    const auto evt = button_.poll(now);
    if (evt == ButtonHandler::Event::ShortPress) {
      CanRecorder::instance().nextStage();
      dashboard_.triggerButtonFeedback(now);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("[BUTTON] Stage changed to %u (%s)\r\n",
                    (unsigned)CanRecorder::instance().currentStage(),
                    CanRecorder::instance().stageName(CanRecorder::instance().currentStage()));
#endif
    } else if (evt == ButtonHandler::Event::LongPress) {
      const bool rec = !CanRecorder::instance().isRecording();
      CanRecorder::instance().setRecording(rec);
      dashboard_.triggerButtonFeedback(now);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("[BUTTON] Recording %s\r\n", rec ? "RESUMED" : "PAUSED");
#endif
    }
  }

  static RpmState initialState() {
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    return RpmState(selectedVehicleProfile());
#elif PULSESHIFT_MODE == PULSESHIFT_CAPTURE
    return RpmState(RpmMapping{});
#else
    return RpmState(demonstrationMapping());
#endif
  }

#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
  static uint32_t receiveBitrate() {
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    const auto& p = selectedVehicleProfile();
    return vehicleProfileReady(p) ? p.bitrate : 0;
#else
    return PULSESHIFT_BITRATE;
#endif
  }
#endif

#if PULSESHIFT_SERIAL_DIAGNOSTICS
  void handleSerialInput() {
    for (unsigned i = 0; i < 32 && Serial.available() > 0; ++i) {
      const char ch = static_cast<char>(Serial.read());
      if (serialProto_.feed(ch)) {
        processCommand(serialProto_.command());
        serialProto_.reset();
      }
    }
  }

  void processCommand(const char* rawCmd) {
    while (*rawCmd == ' ') ++rawCmd;
    if (*rawCmd == '\0') return;

    char cmd[SerialProtocol::MAX_CMD_LEN];
    strncpy(cmd, rawCmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';

    // Normalize command keyword to uppercase for common commands
    char upper[16] = "";
    for (size_t i = 0; i < sizeof(upper) - 1 && cmd[i] && cmd[i] != ' '; ++i) {
      upper[i] = toupper(static_cast<unsigned char>(cmd[i]));
    }

    if (strcmp(upper, "HELP") == 0) {
      printHelp();
    } else if (strcmp(upper, "STATUS") == 0) {
      printStatus();
    } else if (strcmp(upper, "LOGS") == 0) {
      DiagLogger::instance().dumpToSerial();
    } else if (strcmp(upper, "DUMP") == 0) {
      CanRecorder::instance().dumpToSerial();
    } else if (strcmp(cmd, "CLEAR YES") == 0 || strcmp(cmd, "clear yes") == 0) {
      DiagLogger::instance().clear();
      CanRecorder::instance().clear();
      Serial.println("[OK] All offline diagnostics and CAN captures cleared.");
    } else if (strcmp(upper, "CLEAR") == 0) {
      Serial.println("[WARNING] To confirm clearing all stored offline logs, type: CLEAR YES");
    } else if (strncmp(cmd, "stage ", 6) == 0 || strncmp(cmd, "STAGE ", 6) == 0) {
      const int s = atoi(cmd + 6);
      if (s >= 1 && s <= 3) {
        CanRecorder::instance().setStage(static_cast<CanRecorder::Stage>(s));
        dashboard_.triggerButtonFeedback(millis());
        Serial.printf("[STAGE] Set to %d (%s)\r\n", s,
                      CanRecorder::instance().stageName(CanRecorder::instance().currentStage()));
      } else {
        Serial.println("[ERROR] Stage must be 1 (KeyOn/EngineOff), 2 (Idle), or 3 (GentleRPM)");
      }
    }
#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
    else if (strcmp(cmd, "demo start") == 0) {
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
    }
#endif
    else {
      Serial.println("[ERROR] Unknown command. Type 'HELP' for available commands.");
    }
  }

  void printHelp() {
    Serial.println("=== PulseShift Command Interface ===");
    Serial.println("  STATUS          : Show comprehensive system & offline diagnostic status");
    Serial.println("  LOGS            : Dump persistent diagnostic event log");
    Serial.println("  DUMP            : Export raw recorded CAN frames with CRC checksums");
    Serial.println("  STAGE <1|2|3>   : Set field capture stage (1=KeyOn, 2=Idle, 3=GentleRPM)");
    Serial.println("  CLEAR           : Request erasing stored diagnostic logs and CAN capture");
    Serial.println("  CLEAR YES       : Confirm erase of stored logs");
#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
    Serial.println("  demo start      : Resume automated synthetic sweep");
    Serial.println("  demo stop       : Stop synthetic frame reception");
    Serial.println("  rpm <val>       : Inject synthetic RPM (0..20000)");
    Serial.println("  profile <style> : Set style (classic, racing, minimal)");
    Serial.println("  brightness <val>: Set LED brightness (0..255)");
#endif
  }

  void printStatus() {
    auto& sm = StorageManager::instance();
    auto& rec = CanRecorder::instance();
    auto& diag = DiagLogger::instance();
    const uint32_t now = millis();

    Serial.println("=== PULSESHIFT SYSTEM STATUS ===");
    Serial.printf("Firmware: PulseShift v0.3.1 (Arduino ESP32 2.0.17 / ESP-IDF 4.4.7)\r\n");
    Serial.printf("Operating Mode: %s\r\n", config::modeName);
    Serial.printf("Target Vehicle: %s\r\n", config::vehicle);
    Serial.printf("Transceiver: %s (Passive listen-only; TX pin %d disconnected/pulled-up)\r\n",
                  config::transceiver, config::txPin);
    Serial.printf("Uptime: %lu ms | Boot Count: %lu | Last Reset Reason: %s\r\n",
                  (unsigned long)now, (unsigned long)sm.bootCount(), diag.resetReasonString());

    Serial.println("\n--- Storage (LittleFS / NVS) ---");
    Serial.printf("Mounted: %s | Mount Failed: %s\r\n",
                  sm.isMounted() ? "YES" : "NO", sm.mountFailed() ? "YES" : "NO");
    Serial.printf("Total Space: %lu bytes | Used: %lu bytes | Free: %lu bytes\r\n",
                  (unsigned long)sm.totalBytes(), (unsigned long)sm.usedBytes(), (unsigned long)sm.freeBytes());
    Serial.printf("Storage Full: %s\r\n", sm.isStorageFull() ? "YES" : "NO");
    Serial.printf("Diagnostic Log Size: %lu bytes (Max: %lu)\r\n",
                  (unsigned long)sm.size(DiagLogger::LOG_PATH), (unsigned long)DiagLogger::MAX_LOG_SIZE);
    Serial.printf("CAN Capture File Size: %lu bytes (Max: %lu)\r\n",
                  (unsigned long)rec.currentFileSize(), (unsigned long)CanRecorder::MAX_CAPTURE_SIZE);

    Serial.println("\n--- Field CAN Capture ---");
    Serial.printf("Session ID: %lu\r\n", (unsigned long)sm.currentSessionId());
    Serial.printf("Recording Active: %s\r\n", rec.isRecording() ? "YES" : "PAUSED");
    Serial.printf("Current Stage: %u (%s)\r\n", (unsigned)rec.currentStage(), rec.stageName(rec.currentStage()));
    Serial.printf("Frames Recorded: %lu | In-Memory Dropped: %lu\r\n",
                  (unsigned long)rec.recordedFrames(), (unsigned long)rec.droppedFrames());

#if PULSESHIFT_MODE != PULSESHIFT_REPLAY
    Serial.println("\n--- CAN Hardware Controller ---");
    Serial.printf("Configured Bitrate: %lu bps\r\n", (unsigned long)transport_.bitrate());
    Serial.printf("Controller Healthy: %s\r\n", transport_.healthy() ? "YES" : "HALTED");
    Serial.printf("Bus Traffic Active: %s (Last frame: %lu ms ago)\r\n",
                  transport_.hasTraffic(now) ? "YES" : "NO",
                  (unsigned long)(now - transport_.lastRxMs()));
    Serial.printf("Total Frames RX: %lu | Accepted: %lu\r\n",
                  (unsigned long)transport_.totalRx(), (unsigned long)transport_.acceptedFrames());
    Serial.printf("Driver Missed Frames: %lu | Hardware Overruns: %lu\r\n",
                  (unsigned long)transport_.missedFrames(), (unsigned long)transport_.overrunFrames());
    Serial.printf("Bus Errors: %lu | Bus Error Alerts: %lu | RX Queue Full Alerts: %lu\r\n",
                  (unsigned long)transport_.busErrors(), (unsigned long)transport_.busErrorAlerts(),
                  (unsigned long)transport_.rxOverflowAlerts());
#endif

    Serial.println("\n--- RPM Decoder ---");
    Serial.printf("RPM Mapped: %s\r\n", rpm_.mapped() ? "YES" : "NO (Unconfigured / Capture Only)");
    Serial.printf("Accepted RPM Frames: %lu | Rejected: %lu\r\n",
                  (unsigned long)rpm_.accepted(), (unsigned long)rpm_.rejected());
    Serial.printf("Current RPM: %u | Signal Invalid: %s | Fresh: %s\r\n",
                  rpm_.rpm(), rpm_.signalInvalid() ? "YES" : "NO",
                  rpm_.fresh(now, rpm_.timeoutMs(config::staleMs)) ? "YES" : "NO");
    Serial.println("================================");
  }
#endif

  RpmState rpm_;
  Dashboard dashboard_;
  FrameLog logger_;
  ButtonHandler button_;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  SerialProtocol serialProto_;
#endif
#if PULSESHIFT_MODE == PULSESHIFT_REPLAY
  ReplayTransport transport_;
#else
  CanTransport transport_{receiveBitrate()
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    , &logger_
#endif
  };
#endif
  uint32_t lastReport_ = 0;
  bool finalReportPrinted_ = false;
#if PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST
  uint32_t lastSent_ = 0, sequence_ = 0;
#endif
};

Application app;
}  // namespace

void setup() { app.begin(); }
void loop() { app.tick(); }
