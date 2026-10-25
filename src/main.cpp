#include <Arduino.h>
#include "config.h"
#include "rpm_decoder.h"
#include "frame_log.h"
#include "dashboard.h"
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
    Serial.begin(115200);
    Serial.printf("REVIX LINK mode=%s SDK=%s\r\n", config::modeName, ESP.getSdkVersion());
#if REVIX_MODE == REVIX_BIKE
    Serial.printf("TARGET: %s / %s / %u WS2812B LEDs\r\n",
      config::vehicle, config::transceiver, config::ledCount);
    Serial.println("RPM mapping unverified; SN65HVD230 RS must be held HIGH for passive capture");
#endif
#if REVIX_MODE == REVIX_REPLAY
    Serial.println("SYNTHETIC FRAMES ONLY: does not validate TWAI or physical CAN");
#else
    transport_.begin();
#endif
    if (!dashboard_.begin()) Serial.println("OLED INIT FAILED (CAN processing continues)");
  }
  void tick() {
    const uint32_t now = millis();
#if REVIX_MODE != REVIX_REPLAY
    transport_.poll(now);
#endif
    CanFrame f;
    for (unsigned i=0; i<32; ++i) {
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
    if (transport_.canSend() && uint32_t(now-lastSent_) >= config::framePeriodMs) {
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
    if (!finalReportPrinted_ && uint32_t(now-lastReport_) >= 1000) {
      lastReport_ = now;
      Serial.printf("APP mode=%s accepted=%lu rejected=%lu fresh=%d busFresh=%d rpm=%u logDropped=%lu\r\n",
        config::modeName, (unsigned long)rpm_.accepted(), (unsigned long)rpm_.rejected(),
        rpm_.fresh(now,config::staleMs) && healthy, rpm_.busFresh(now,config::staleMs), rpm_.rpm(),
        (unsigned long)logger_.dropped());
#if REVIX_MODE != REVIX_REPLAY
      transport_.diagnostics();
      // After a terminal fault, retain one final stopped-state report then
      // leave the terminal quiet. The dashboard still refreshes normally.
      if (!healthy) finalReportPrinted_ = true;
#endif
    }
  }
 private:
  static RpmMapping mapping() {
#if REVIX_MODE == REVIX_BIKE
    return RpmMapping{}; // Deliberately disabled: no guessed motorcycle mapping.
#else
    return demonstrationMapping();
#endif
  }
  RpmState rpm_;
  Dashboard dashboard_;
  FrameLog logger_;
#if REVIX_MODE == REVIX_REPLAY
  ReplayTransport transport_;
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
}
void setup() { app.begin(); }
void loop() { app.tick(); }
