// ============================================================================
// PulseShift: ESP32 Motorcycle Shift Light & CAN Diagnostic Logger
// Copyright (c) 2026 Aritra Saha. All rights reserved.
//
// Arduino IDE Setup:
//   1. Open PulseShift.ino in Arduino IDE.
//   2. Tools -> Board -> ESP32 Arduino -> "ESP32 Dev Module"
//   3. Tools -> Partition Scheme -> "Default 4MB with spiffs"
//   4. Tools -> Upload Speed -> "921600"
//   5. Tools -> Port -> (Select your ESP32 COM port)
//   6. Click Upload (Ctrl+U).
//
// Configuration:
//   Edit UserConfig.h in this folder to change operating mode, RPM thresholds,
//   LED pin, LED count, brightness, or CAN bitrate.
//
// Serial Monitor (115200 baud):
//   Commands:
//     STATUS          : System health, storage state, and troubleshooting advice
//     LOGS            : Dump persistent diagnostic events log
//     DUMP            : Export CAN capture in 500-record pages
//     DUMP NEXT       : Export next 500-record page
//     DUMP ALL        : Export all records continuously without pagination
//     DUMP STATUS     : Show export progress and page count
//     DUMP CANCEL     : Reset export cursor to page 1
//     DUMP PAGE <n>   : Export specific page number
//     STAGE <1|2|3>   : Set capture stage (1=KeyOn, 2=Idle, 3=GentleRPM)
//     1, 2, 3         : Quick shortcuts for capture stages
//     CAPTURE STOP    : Pause flash recording and flush buffers
//     CAPTURE RESUME  : Resume flash recording
//     STREAM ON       : Enable live raw CAN frame streaming over serial
//     STREAM OFF      : Disable live streaming (default: concise summaries only)
//     CLEAR           : Request erasing stored diagnostic logs and CAN capture
//     CLEAR YES       : Confirm erase and start a fresh capture session
//     HELP            : Display this command reference
// ============================================================================

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "UserConfig.h"
#include "src/config.h"
#include "src/rpm_decoder.h"
#include "src/frame_log.h"
#include "src/dashboard.h"
#include "src/storage_manager.h"
#include "src/diag_logger.h"
#include "src/can_recorder.h"
#include "src/button_handler.h"
#include "src/serial_protocol.h"
#include "src/rpm_candidate.h"

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY || PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
#include "src/simulated_ecu.h"
#endif
#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
#include "src/can_transport.h"
#endif

namespace {
class Application {
public:
  Application()
    : rpm_(initialState()) {}

  void begin() {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.begin(config::serialBaud);
    Serial.printf("PulseShift mode=%s SDK=%s\r\n", config::modeName, ESP.getSdkVersion());
#endif

    // Initialize non-volatile persistent storage (LittleFS & Preferences NVS)
    auto& sm = StorageManager::instance();
    const bool fsOk = sm.begin(false);  // Non-destructive mount

    // Initialize diagnostic logger
    auto& diag = DiagLogger::instance();
    diag.begin(sm.bootCount(), sm.currentSessionId());
    diag.logBoot(config::modeName, rpm_.mapped());
    diag.logStorageState(fsOk, sm.usedBytes(), sm.freeBytes(), sm.isStorageFull());

    // Initialize raw CAN recorder
#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
    const uint32_t bitrate = receiveBitrate();
#else
    const uint32_t bitrate = 0;
#endif
    CanRecorder::instance().begin(sm.currentSessionId(), sm.bootCount(), bitrate, config::modeName, config::vehicle);

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("META,baud=%lu,bitrate=%lu,mode=%s,vehicle=%s,session=%lu,boot=%lu\r\n",
                  (unsigned long)config::serialBaud,
                  (unsigned long)bitrate,
                  config::modeName, config::vehicle,
                  (unsigned long)sm.currentSessionId(),
                  (unsigned long)sm.bootCount());
#endif

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
    Serial.println("RECEIVE ONLY: Breakout CTX tied to 3V3 directly, GPIO17 disconnected, software listen-only");
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    Serial.printf("PROFILE id=%s status=%s ready=%d\r\n", selectedVehicleProfile().identifier,
                  profileStatusName(selectedVehicleProfile().status), rpm_.mapped());
#else
    Serial.println("CAN_CAPTURE: raw frames and offline diagnostics active");
#endif
#endif
#endif

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
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
#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
    transport_.poll(now);
#endif

    // 4. Ingest incoming CAN frames
    CanFrame f;
    for (unsigned i = 0; i < 128; ++i) {
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
      const bool got = transport_.receive(now, f);
#else
      const bool got = transport_.receive(f, now);
#endif
      if (!got) break;

      dashboard_.notifyRxActivity(now);
      const auto result = rpm_.ingest(f);
      candidateMonitor_.ingest(f, now);

      if (!firstFrameSeen_) {
        firstFrameSeen_ = true;
#if PULSESHIFT_SERIAL_DIAGNOSTICS
        Serial.println("[CAN] First motorcycle CAN frame received! Traffic is active.");
#endif
      }

#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
      CanRecorder::instance().push(f);
#endif

      // Real-time USB Serial streaming (only when live streaming is enabled)
      if (liveStreamEnabled_) {
        streamFrameSerial(f, static_cast<uint8_t>(CanRecorder::instance().currentStage()));
      }
    }

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
    if (transport_.canSend() && uint32_t(now - lastSent_) >= config::framePeriodMs) {
      if (transport_.sendTest(demonstrationFrame(now, sequence_), now)) ++sequence_;
      lastSent_ = now;
    }
#endif

#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
    constexpr bool healthy = true, configured = true;
#else
    const bool healthy = transport_.healthy();
    const bool configured = transport_.configured()
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
                            && rpm_.mapped()
#endif
      ;
#endif

    // 5. Service storage queues and periodic diagnostic flushes
    CanRecorder::instance().pump(now);
    DiagLogger::instance().pump(now);

    // 6. Evaluate stale timeouts and refresh UI
    rpm_.expire(now, rpm_.timeoutMs(config::staleMs), config::staleMs);
    logger_.pump();
    dashboard_.update(rpm_, now, healthy, configured, rpm_.timeoutMs(config::staleMs));

    // Handle shift-light bench test timeout (rollover-safe)
    if (dashboard_.isTestRpmActive() && uint32_t(now - rpmTestStartMs_) >= 10000) {
      dashboard_.setTestRpm(0, false);
    }

#if PULSESHIFT_SERIAL_DIAGNOSTICS
    // Rate-limited live RPM candidate streaming (10 Hz, 100 ms)
    if (rpmMonitorStreaming_ && uint32_t(now - lastRpmMonitorPrint_) >= 100) {
      lastRpmMonitorPrint_ = now;
      candidateMonitor_.printStreamLine(now);
    }
    // 7. Periodic concise live summary every 2 seconds
    if (!finalReportPrinted_ && uint32_t(now - lastLiveSummary_) >= 2000) {
      lastLiveSummary_ = now;
      printLiveSummary(now);
#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
      if (!healthy) finalReportPrinted_ = true;
#endif
    }

    // 8. Structured machine-readable telemetry every 5 seconds
    if (uint32_t(now - lastStat_) >= 5000) {
      lastStat_ = now;
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
      Serial.printf("STAT,%lu,0,0,0,0,0,%lu,%lu,%lu\r\n",
                    (unsigned long)now,
                    (unsigned long)serialDroppedFrames_,
                    (unsigned long)CanRecorder::instance().recordedFrames(),
                    (unsigned long)CanRecorder::instance().droppedFrames());
#else
      Serial.printf("STAT,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
                    (unsigned long)now,
                    (unsigned long)transport_.totalRx(),
                    (unsigned long)transport_.acceptedFrames(),
                    (unsigned long)transport_.missedFrames(),
                    (unsigned long)transport_.overrunFrames(),
                    (unsigned long)transport_.busErrors(),
                    (unsigned long)serialDroppedFrames_,
                    (unsigned long)CanRecorder::instance().recordedFrames(),
                    (unsigned long)CanRecorder::instance().droppedFrames());
#endif
    }
#endif
  }

private:
  void streamFrameSerial(const CanFrame& f, uint8_t stage) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    if (!liveStreamEnabled_) return;

    if (Serial.availableForWrite() < 50) {
      ++serialDroppedFrames_;
      if (serialDroppedFrames_ == 1 || (serialDroppedFrames_ % 100) == 0) {
        DiagLogger::instance().logEvent(DiagLevel::Warn, "SERIAL_BOTTLENECK",
                                       "Serial buffer congested; dropped=%lu",
                                       (unsigned long)serialDroppedFrames_);
      }
      return;
    }

    char hexData[17] = "";
    const uint8_t len = (f.dlc <= 8) ? f.dlc : 8;
    for (uint8_t i = 0; i < len; ++i) {
      char byteStr[3];
      snprintf(byteStr, sizeof(byteStr), "%02X", f.data[i]);
      strcat(hexData, byteStr);
    }

    Serial.printf("FRAME,%lu,%u,%0*lX,%d,%d,%u,%s\r\n",
                  (unsigned long)f.timestampMs,
                  stage,
                  f.extended ? 8 : 3,
                  (unsigned long)f.id,
                  f.extended ? 1 : 0,
                  f.remote ? 1 : 0,
                  f.dlc,
                  hexData);
#else
    (void)f; (void)stage;
#endif
  }

  void setStage(CanRecorder::Stage s) {
    CanRecorder::instance().setStage(s);
    dashboard_.triggerButtonFeedback(millis());
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    Serial.printf("STAGE,%lu,%u,%s\r\n", (unsigned long)millis(), (unsigned)s,
                  CanRecorder::instance().stageName(s));
    Serial.printf("[STAGE] Set to Stage %u (%s)\r\n", (unsigned)s,
                  CanRecorder::instance().stageName(s));
#endif
  }

  void printLiveSummary(uint32_t now) {
#if PULSESHIFT_SERIAL_DIAGNOSTICS
    auto& rec = CanRecorder::instance();
#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
    const bool traffic = transport_.hasTraffic(now);
    const bool healthy = transport_.healthy();
    const uint32_t br = transport_.bitrate();
    const uint32_t rxTotal = transport_.totalRx();
#else
    const bool traffic = true;
    const bool healthy = true;
    const uint32_t br = 500000;
    const uint32_t rxTotal = rec.recordedFrames();
#endif
    const char* stageStr = rec.stageName(rec.currentStage());
    const size_t bytes = rec.currentFileSize();

    if (rec.state() == CanRecorder::State::DamagedRecording) {
      Serial.printf("[LIVE] Bus: %s (%lu bps) | Flash: DAMAGED PRESERVED (%lu B) | Type 'CLEAR YES' to reinitialize\r\n",
                    healthy ? "OK" : "FAULT", (unsigned long)br, (unsigned long)bytes);
    } else if (rec.state() == CanRecorder::State::PreservedPriorSession) {
      Serial.printf("[LIVE] Bus: %s (%lu bps) | Flash: PRESERVED (%lu B) | Type 'DUMP' to export or 'CLEAR YES' to wipe\r\n",
                    healthy ? "OK" : "FAULT", (unsigned long)br, (unsigned long)bytes);
    } else if (rec.state() == CanRecorder::State::StorageFull) {
      Serial.printf("[LIVE] Bus: %s (%lu bps) | Flash: FULL (%lu B) | Rec: %lu | Drops: %lu | Type 'DUMP' to export\r\n",
                    healthy ? "OK" : "FAULT", (unsigned long)br,
                    (unsigned long)bytes, (unsigned long)rec.recordedFrames(), (unsigned long)rec.droppedFrames());
    } else if (rec.state() == CanRecorder::State::Paused) {
      Serial.printf("[LIVE] Bus: %s (%lu bps) | Recording: PAUSED | Stage %u (%s) | Rec: %lu | Flash: %lu B\r\n",
                    healthy ? "OK" : "FAULT", (unsigned long)br,
                    (unsigned)rec.currentStage(), stageStr,
                    (unsigned long)rec.recordedFrames(), (unsigned long)bytes);
    } else if (!traffic && rxTotal == 0) {
      Serial.printf("[LIVE] Bus: %s (%lu bps) | Traffic: WAITING | Stage %u (%s) | Rec: 0 | Flash: %lu B\r\n",
                    healthy ? "OK" : "FAULT", (unsigned long)br,
                    (unsigned)rec.currentStage(), stageStr, (unsigned long)bytes);
    } else {
      Serial.printf("[LIVE] Bus: %s | Traffic: %s | Stage %u (%s) | RX: %lu | Rec: %lu | Drops: %lu | Flash: %lu B\r\n",
                    healthy ? "OK" : "FAULT", traffic ? "ACTIVE" : "IDLE",
                    (unsigned)rec.currentStage(), stageStr,
                    (unsigned long)rxTotal,
                    (unsigned long)rec.recordedFrames(),
                    (unsigned long)rec.droppedFrames(),
                    (unsigned long)bytes);
    }
#else
    (void)now;
#endif
  }

  void handleButton(uint32_t now) {
    const auto evt = button_.poll(now);
    if (evt == ButtonHandler::Event::ShortPress) {
      CanRecorder::instance().nextStage();
      dashboard_.triggerButtonFeedback(now);
#if PULSESHIFT_SERIAL_DIAGNOSTICS
      Serial.printf("STAGE,%lu,%u,%s\r\n", (unsigned long)now,
                    (unsigned)CanRecorder::instance().currentStage(),
                    CanRecorder::instance().stageName(CanRecorder::instance().currentStage()));
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
#elif defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
    return RpmState(demonstrationMapping());
#else
    return RpmState(RpmMapping{});
#endif
  }

#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
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
    } else if (strcasecmp(cmd, "RPM STATUS") == 0) {
      candidateMonitor_.printStatus(millis(), transport_.healthy(), transport_.bitrate(), transport_.hasTraffic(millis()));
    } else if (strcasecmp(cmd, "RPM MONITOR ON") == 0) {
      rpmMonitorStreaming_ = true;
      Serial.println("[RPM] Rate-limited live candidate streaming ENABLED (10 Hz). Type 'RPM MONITOR OFF' to stop.");
    } else if (strcasecmp(cmd, "RPM MONITOR OFF") == 0) {
      rpmMonitorStreaming_ = false;
      Serial.println("[RPM] Rate-limited live candidate streaming DISABLED.");
    } else if (strcasecmp(cmd, "RPM MONITOR") == 0) {
      candidateMonitor_.printStreamLine(millis());
    } else if (strcasecmp(cmd, "RPM TEST OFF") == 0) {
      dashboard_.setTestRpm(0, false);
      Serial.println("[TEST] Shift-light bench test disabled. Returned to field capture status LEDs.");
    } else if (strncasecmp(cmd, "RPM TEST ", 9) == 0) {
      uint32_t val = 0;
      if (parseUnsignedNumber(cmd + 9, 20000, val)) {
        dashboard_.setTestRpm(static_cast<uint16_t>(val), true);
        rpmTestStartMs_ = millis();
        Serial.printf("[TEST] 6-LED shift-light bench test active: %u RPM for 10s (Physical CAN shift lights remain DISABLED)\r\n", (unsigned)val);
      } else {
        Serial.println("[ERROR] Test RPM must be a valid integer between 0 and 20000.");
      }
    } else if (strncasecmp(cmd, "RPM MULTIPLIER ", 15) == 0) {
      char* endptr = nullptr;
      const char* p = cmd + 15;
      while (*p == ' ' || *p == '\t') ++p;
      const float m = strtof(p, &endptr);
      while (endptr && (*endptr == ' ' || *endptr == '\t')) ++endptr;
      if (endptr && endptr != p && *endptr == '\0' && !isnan(m) && !isinf(m) && m > 0.0f && m <= 1000.0f) {
        candidateMonitor_.setMultiplier(m);
        Serial.printf("[RPM] Candidate multiplier updated to %.1f (Calibration remains UNVERIFIED)\r\n", m);
      } else {
        Serial.println("[ERROR] Multiplier must be a valid positive number between 0.1 and 1000.0.");
      }
    } else if (strcmp(upper, "LOGS") == 0) {
      DiagLogger::instance().dumpToSerial();
    } else if (strcasecmp(cmd, "DUMP") == 0 || strcasecmp(cmd, "DUMP NEXT") == 0) {
      CanRecorder::instance().dumpNext();
    } else if (strcasecmp(cmd, "DUMP ALL") == 0) {
      CanRecorder::instance().dumpAll();
    } else if (strcasecmp(cmd, "DUMP STATUS") == 0) {
      CanRecorder::instance().dumpStatus();
    } else if (strcasecmp(cmd, "DUMP CANCEL") == 0 || strcasecmp(cmd, "DUMP RESET") == 0) {
      CanRecorder::instance().dumpCancel();
    } else if (strncasecmp(cmd, "DUMP PAGE ", 10) == 0) {
      uint32_t pg = 0;
      if (parseUnsignedNumber(cmd + 10, 1000000, pg) && pg > 0) {
        CanRecorder::instance().dumpPageNumber(pg);
      } else {
        Serial.println("[ERROR] Page must be a valid integer 1 or higher.");
      }
    } else if (strcasecmp(cmd, "CAPTURE STOP") == 0 || strcasecmp(cmd, "CAPTURE PAUSE") == 0) {
      CanRecorder::instance().flush();
      CanRecorder::instance().setRecording(false);
      Serial.println("[CAPTURE] Recording paused. Frames in memory flushed to flash.");
    } else if (strcasecmp(cmd, "CAPTURE RESUME") == 0 || strcasecmp(cmd, "CAPTURE START") == 0) {
      if (CanRecorder::instance().isDamagedRecording()) {
        Serial.println("[CAPTURE] Cannot resume: flash contains damaged or incomplete recording.");
        Serial.println("[CAPTURE] Type 'CLEAR YES' to reinitialize a fresh capture.");
      } else if (CanRecorder::instance().isFlashPreserved()) {
        Serial.println("[CAPTURE] Cannot resume: flash contains preserved capture from prior session.");
        Serial.println("[CAPTURE] Type 'DUMP' to export, or 'CLEAR YES' to start a fresh capture.");
      } else if (CanRecorder::instance().isStorageFull()) {
        Serial.println("[CAPTURE] Cannot resume: storage is full. Type 'DUMP' to export, or 'CLEAR YES' to wipe.");
      } else {
        CanRecorder::instance().setRecording(true);
        Serial.println("[CAPTURE] Recording resumed.");
      }
    } else if (strcmp(cmd, "1") == 0 || strncasecmp(cmd, "STAGE 1", 7) == 0 || strcasecmp(cmd, "STAGE 1") == 0) {
      setStage(CanRecorder::STAGE_KEY_ON_ENGINE_OFF);
    } else if (strcmp(cmd, "2") == 0 || strncasecmp(cmd, "STAGE 2", 7) == 0 || strcasecmp(cmd, "STAGE 2") == 0) {
      setStage(CanRecorder::STAGE_ENGINE_IDLING);
    } else if (strcmp(cmd, "3") == 0 || strncasecmp(cmd, "STAGE 3", 7) == 0 || strcasecmp(cmd, "STAGE 3") == 0) {
      setStage(CanRecorder::STAGE_GENTLE_RPM);
    } else if (strcasecmp(cmd, "STREAM ON") == 0 || strcasecmp(cmd, "LIVE ON") == 0) {
      liveStreamEnabled_ = true;
      Serial.println("[STREAM] Live raw CAN frame streaming ENABLED.");
    } else if (strcasecmp(cmd, "STREAM OFF") == 0 || strcasecmp(cmd, "LIVE OFF") == 0) {
      liveStreamEnabled_ = false;
      Serial.println("[STREAM] Live raw CAN frame streaming DISABLED (default).");
    } else if (strcasecmp(cmd, "CLEAR YES") == 0) {
      DiagLogger::instance().clear();
      CanRecorder::instance().clear();
      Serial.println("[OK] Storage cleared. Fresh capture session initialized.");
    } else if (strcasecmp(cmd, "CLEAR") == 0) {
      Serial.println("[WARNING] To confirm clearing all stored offline logs and capture, type: CLEAR YES");
    }
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
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
    Serial.println("  STATUS          : Comprehensive system status & intelligent troubleshooting");
    Serial.println("  LOGS            : Dump persistent diagnostic event log");
    Serial.println("  RPM STATUS      : Candidate 0x301 engine-speed monitor & calibration status");
    Serial.println("  RPM MONITOR ON  : Enable rate-limited (10 Hz) 0x301 candidate streaming");
    Serial.println("  RPM MONITOR OFF : Disable candidate streaming");
    Serial.println("  RPM TEST <val>  : Bench test 6-LED shift lights with simulated RPM (0..20000)");
    Serial.println("  RPM TEST OFF    : Turn off shift-light bench test");
    Serial.println("  DUMP            : Export recorded CAN frames in 500-record pages");
    Serial.println("  DUMP NEXT       : Export next 500-record page");
    Serial.println("  DUMP ALL        : Export all records continuously without pagination");
    Serial.println("  DUMP STATUS     : Show current export progress and page count");
    Serial.println("  DUMP CANCEL     : Reset export cursor to page 1");
    Serial.println("  DUMP PAGE <n>   : Export specific page number n");
    Serial.println("  STAGE <1|2|3>   : Set field capture stage (1=KeyOn, 2=Idle, 3=GentleRPM)");
    Serial.println("  1, 2, 3         : Quick shortcuts to set Stage 1, 2, or 3");
    Serial.println("  CAPTURE STOP    : Pause flash recording and flush buffers");
    Serial.println("  CAPTURE RESUME  : Resume flash recording");
    Serial.println("  STREAM ON       : Enable live raw CAN frame streaming over serial");
    Serial.println("  STREAM OFF      : Disable live streaming (default: concise summaries only)");
    Serial.println("  CLEAR           : Request erasing stored diagnostic logs and CAN capture");
    Serial.println("  CLEAR YES       : Confirm erase and start a fresh capture session");
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
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
#if defined(ESP_ARDUINO_VERSION_MAJOR)
    Serial.printf("Firmware: PulseShift (Arduino ESP32 core %d.%d.%d / ESP-IDF %s / built %s %s)\r\n",
                  ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH,
                  ESP.getSdkVersion(), __DATE__, __TIME__);
#else
    Serial.printf("Firmware: PulseShift (ESP-IDF %s / built %s %s)\r\n",
                  ESP.getSdkVersion(), __DATE__, __TIME__);
#endif
    Serial.printf("Operating Mode: %s\r\n", config::modeName);
    Serial.printf("Target Vehicle: %s\r\n", config::vehicle);
    Serial.printf("Transceiver: %s (Passive listen-only; GPIO%d disconnected, CTX to 3V3 directly)\r\n",
                  config::transceiver, config::txPin);
    Serial.printf("Uptime: %lu ms | Boot Count: %lu | Last Reset Reason: %s\r\n",
                  (unsigned long)now, (unsigned long)sm.bootCount(), diag.resetReasonString());

    Serial.println("\n--- Storage (LittleFS / NVS) ---");
    Serial.printf("Mounted: %s | Mount Failed: %s\r\n",
                  sm.isMounted() ? "YES" : "NO", sm.mountFailed() ? "YES" : "NO");
    Serial.printf("Total Space: %lu bytes | Used: %lu bytes | Free: %lu bytes\r\n",
                  (unsigned long)sm.totalBytes(), (unsigned long)sm.usedBytes(), (unsigned long)sm.freeBytes());
    Serial.printf("Storage Full: %s | Capture Preserved: %s | Damaged Preserved: %s\r\n",
                  sm.isStorageFull() ? "YES" : "NO",
                  rec.isFlashPreserved() ? "YES" : "NO",
                  rec.isDamagedRecording() ? "YES" : "NO");
    Serial.printf("Diagnostic Log Size: %lu bytes (Max: %lu)\r\n",
                  (unsigned long)sm.size(DiagLogger::LOG_PATH), (unsigned long)DiagLogger::MAX_LOG_SIZE);
    Serial.printf("CAN Capture File Size: %lu bytes (Max: %lu)\r\n",
                  (unsigned long)rec.currentFileSize(), (unsigned long)CanRecorder::MAX_CAPTURE_SIZE);

    Serial.println("\n--- Field CAN Capture ---");
    Serial.printf("Session ID: %lu\r\n", (unsigned long)sm.currentSessionId());
    Serial.printf("Recording State: %s\r\n", rec.stateName(rec.state()));
    Serial.printf("Current Stage: %u (%s)\r\n", (unsigned)rec.currentStage(), rec.stageName(rec.currentStage()));
    Serial.printf("Frames Recorded: %lu | In-Memory Dropped: %lu | Serial Dropped: %lu\r\n",
                  (unsigned long)rec.recordedFrames(), (unsigned long)rec.droppedFrames(),
                  (unsigned long)serialDroppedFrames_);
    Serial.printf("Export Cursor: Record %lu of %lu (Page %lu of %lu)\r\n",
                  (unsigned long)rec.dumpCursor(), (unsigned long)rec.totalRecords(),
                  (unsigned long)((rec.dumpCursor() / CanRecorder::DUMP_PAGE_SIZE) + 1),
                  (unsigned long)rec.totalPages());
    Serial.printf("Live Raw Frame Streaming: %s (use 'STREAM ON' or 'STREAM OFF')\r\n",
                  liveStreamEnabled_ ? "ENABLED" : "DISABLED (Default)");

#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
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

    Serial.println("\n--- Live RPM Candidate Monitor (CAN ID 0x301) ---");
    Serial.printf("Candidate ID: 0x%03lX (Standard 11-bit) | Byte: %u | Multiplier: %.1f\r\n",
                  (unsigned long)candidateMonitor_.candidateId(),
                  (unsigned)candidateMonitor_.byteIndex(),
                  candidateMonitor_.multiplier());
    if (candidateMonitor_.receivedCount() > 0 && candidateMonitor_.isFresh(now)) {
      Serial.printf("Raw Value: %u (0x%02X) | Estimated RPM: %u | Signal Fresh: YES\r\n",
                    (unsigned)candidateMonitor_.rawValue(),
                    (unsigned)candidateMonitor_.rawValue(),
                    (unsigned)candidateMonitor_.estimatedRpm(now));
      Serial.printf("Last Update: %lu ms ago | Freshness Window: %lu ms\r\n",
                    (unsigned long)candidateMonitor_.lastUpdateAgeMs(now),
                    (unsigned long)candidateMonitor_.freshnessTimeoutMs());
    } else if (candidateMonitor_.receivedCount() > 0) {
      Serial.printf("Raw Value: %u (0x%02X) | Estimated RPM: INVALID (STALE) | Signal Fresh: NO (STALE)\r\n",
                    (unsigned)candidateMonitor_.rawValue(),
                    (unsigned)candidateMonitor_.rawValue());
      Serial.printf("Last Update: %lu ms ago | Freshness Window: %lu ms\r\n",
                    (unsigned long)candidateMonitor_.lastUpdateAgeMs(now),
                    (unsigned long)candidateMonitor_.freshnessTimeoutMs());
    } else {
      Serial.println("Raw Value: -- | Estimated RPM: -- | Signal Fresh: NO (WAITING)");
      Serial.println("Last Update: -- | Freshness Window: 250 ms");
    }
    Serial.printf("Frames Received: %lu | Rejected: %lu\r\n",
                  (unsigned long)candidateMonitor_.receivedCount(),
                  (unsigned long)candidateMonitor_.rejectedCount());
    if (candidateMonitor_.receivedCount() > 0) {
      Serial.printf("Observed Range: Min: %u | Max: %u\r\n",
                    (unsigned)candidateMonitor_.minObservedRaw(),
                    (unsigned)candidateMonitor_.maxObservedRaw());
    }
    Serial.println("Calibration: UNVERIFIED (Keep RPM_MAPPED=NO until physical tachometer verified)");
    Serial.printf("Physical Shift Lights: %s | CAN TX: SOFTWARE MUTED (Verify CTX->3V3 & GPIO17 air-gap)\r\n",
                  dashboard_.isTestRpmActive() ? "BENCH TEST OVERRIDE" : "DISABLED");

    Serial.println("\n--- Intelligent Diagnostics & Troubleshooting ---");
    if (!sm.isMounted() || sm.mountFailed()) {
      Serial.println("DIAGNOSTIC: STORAGE_ERROR");
      Serial.println("  Observation: LittleFS filesystem mount failed.");
      Serial.println("  Possible Causes: Unformatted flash partition on fresh board, or flash hardware fault.");
      Serial.println("  Recommendation: Type 'CLEAR YES' to format the partition for first-time use.");
    } else if (rec.isDamagedRecording()) {
      Serial.println("DIAGNOSTIC: DAMAGED_CAPTURE_PRESERVED");
      Serial.println("  Observation: Incomplete or damaged capture file is preserved on flash.");
      Serial.println("  Possible Causes: Unexpected power loss during header write or flash corruption.");
      Serial.println("  Recommendation: Type 'CLEAR YES' to start a fresh capture.");
    } else if (rec.isFlashPreserved()) {
      Serial.println("DIAGNOSTIC: FLASH_CAPTURE_PRESERVED");
      Serial.println("  Observation: Valid capture data from a prior session is preserved on flash.");
      Serial.println("  Possible Causes: Board was rebooted or reconnected via USB after a test run.");
      Serial.println("  Recommendation: Type 'DUMP' to export data, or 'CLEAR YES' to start a fresh capture.");
    } else if (rec.isStorageFull()) {
      Serial.println("DIAGNOSTIC: STORAGE_FULL");
      Serial.println("  Observation: Capture storage reached its limit. Recording stopped safely.");
      Serial.println("  Possible Causes: Long recording session reached 1,000,000 byte quota.");
      Serial.println("  Recommendation: Type 'DUMP' to export recorded data.");
    } else if (rec.state() == CanRecorder::State::RecordingOk) {
      Serial.println("DIAGNOSTIC: RECORDING_OK");
      Serial.println("  Observation: Flash recording is active and operational.");
    }

#if !defined(PULSESHIFT_TEST_BUILD) || (PULSESHIFT_MODE != PULSESHIFT_REPLAY)
    if (!transport_.healthy()) {
      Serial.println("DIAGNOSTIC: CAN_CONTROLLER_FAILURE");
      Serial.println("  Observation: Hardware TWAI controller entered bus-off or failed initialization.");
      Serial.println("  Possible Causes: Transceiver 3.3V disconnected, electrical short, or transceiver RS pin floating.");
      Serial.println("  Recommendation: Check transceiver wiring and reboot ESP32.");
    } else if (transport_.totalRx() == 0 || !transport_.hasTraffic(now)) {
      Serial.println("DIAGNOSTIC: NO_CAN_TRAFFIC");
      Serial.println("  Observation: No CAN frames received in the last active interval.");
      Serial.println("  Possible Causes: Ignition is OFF; CAN-H/CAN-L disconnected or reversed; transceiver power missing; bitrate mismatch (configured: 500k); bus in sleep mode.");
      Serial.println("  Recommendations:");
      Serial.println("    1. Turn motorcycle ignition switch ON.");
      Serial.println("    2. Verify SN65HVD230 VCC connects to ESP32 3V3 and GND connects to GND.");
      Serial.println("    3. Verify breakout CTX connects directly to ESP32 3V3 (NO series resistor).");
      Serial.println("    4. Verify breakout CRX connects to ESP32 GPIO16, and GPIO17 is disconnected.");
      Serial.println("    5. Verify CAN-H and CAN-L connections to vehicle diagnostic port.");
    }

    if (transport_.busErrors() > 0) {
      Serial.println("DIAGNOSTIC: CAN_BUS_ERRORS");
      Serial.printf("  Observation: %lu hardware bus error events recorded.\r\n", (unsigned long)transport_.busErrors());
      Serial.println("  Possible Causes: Signal reflection (missing termination), electrical noise, loose ground, or bitrate tolerance mismatch.");
      Serial.println("  Recommendation: Check 120-ohm bus termination resistor and ensure common vehicle ground.");
    }

    if (transport_.overrunFrames() > 0 || transport_.missedFrames() > 0) {
      Serial.println("DIAGNOSTIC: CAN_RX_OVERFLOW");
      Serial.printf("  Observation: %lu driver missed frames, %lu hardware overruns.\r\n",
                    (unsigned long)transport_.missedFrames(), (unsigned long)transport_.overrunFrames());
      Serial.println("  Explanation: Heavy bus traffic briefly exceeded buffer capacity.");
      Serial.println("  Impact: Non-fatal. Dropped frames are logged, and reception continues uninterrupted.");
    }
#endif

    if (!rpm_.mapped()) {
      Serial.println("DIAGNOSTIC: RPM_UNCONFIGURED");
      Serial.println("  Observation: Vehicle profile does not have a verified RPM arbitration ID.");
      Serial.println("  Explanation: Expected for CAN_CAPTURE mode. Export capture with 'DUMP' to identify RPM frame.");
    }

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
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_REPLAY)
  ReplayTransport transport_;
#else
  CanTransport transport_ {
    receiveBitrate()
#if PULSESHIFT_MODE == PULSESHIFT_CAPTURE || PULSESHIFT_MODE == PULSESHIFT_VEHICLE
      ,
      &logger_
#endif
  };
#endif
  uint32_t lastReport_ = 0;
  uint32_t lastLiveSummary_ = 0;
  uint32_t lastStat_ = 0;
  uint32_t serialDroppedFrames_ = 0;
  bool liveStreamEnabled_ = false;
  bool firstFrameSeen_ = false;
  bool finalReportPrinted_ = false;
  RpmCandidateMonitor candidateMonitor_;
  bool rpmMonitorStreaming_ = false;
  uint32_t lastRpmMonitorPrint_ = 0;
  uint32_t rpmTestStartMs_ = 0;
#if defined(PULSESHIFT_TEST_BUILD) && (PULSESHIFT_MODE == PULSESHIFT_TWAI_TEST)
  uint32_t lastSent_ = 0, sequence_ = 0;
#endif
};

Application app;
}  // namespace

void setup() {
  app.begin();
}
void loop() {
  app.tick();
}
