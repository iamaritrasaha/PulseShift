// Copyright (c) 2026 Aritra Saha. All rights reserved.
// Isolated Wokwi investigation only. Actively transmits artificial CAN frames.
// Never package or flash this probe for a motorcycle connection.
#ifndef REVIX_WOKWI_TWAI_PROBE
#error "Build this probe only with explicit REVIX_WOKWI_TWAI_PROBE"
#endif
#include <Arduino.h>
#include <driver/twai.h>
#include <soc/twai_struct.h>
#ifndef PROBE_SINGLE_SHOT
#define PROBE_SINGLE_SHOT 1
#endif
#ifndef PROBE_TX_QUEUE
#define PROBE_TX_QUEUE 0
#endif
#ifndef PROBE_REFERENCE
#define PROBE_REFERENCE 0
#endif

namespace {
bool active=false,done=false;
uint32_t began=0,lastReport=0,seen=0,received=0,completed=0;
void report(const char* stage) {
  twai_status_info_t s{};
  const auto err=twai_get_status_info(&s);
  // Read only non-destructive status/config registers. Reading IR here would
  // clear interrupt flags and could create the failure we are investigating.
  const uint32_t sr=TWAI.status_reg.val;
  Serial.printf("PROBE %s t=%lu err=%s state=%d pendingTX=%lu RX=%lu completed=%lu alerts=0x%08lX TEC=%lu REC=%lu failed=%lu busErrors=%lu SR=0x%02lX TBS=%lu TCS=%lu MOD=0x%02lX IER=0x%02lX\r\n",
    stage,(unsigned long)millis(),esp_err_to_name(err),int(s.state),(unsigned long)s.msgs_to_tx,
    (unsigned long)received,(unsigned long)completed,(unsigned long)seen,
    (unsigned long)s.tx_error_counter,(unsigned long)s.rx_error_counter,
    (unsigned long)s.tx_failed_count,(unsigned long)s.bus_error_count,
    (unsigned long)sr,(unsigned long)((sr>>2)&1),(unsigned long)((sr>>3)&1),
    (unsigned long)TWAI.mode_reg.val,(unsigned long)TWAI.interrupt_enable_reg.val);
}
}
void setup() {
  Serial.begin(115200);
  Serial.printf("REVIX TWAI PROBE SDK=%s self=1 ss=%d queue=%d reference=%d\r\n",
    ESP.getSdkVersion(),PROBE_SINGLE_SHOT,PROBE_TX_QUEUE,PROBE_REFERENCE);
  twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_17,GPIO_NUM_16,TWAI_MODE_NO_ACK);
  g.tx_queue_len=PROBE_TX_QUEUE; g.rx_queue_len=8;
  g.alerts_enabled=TWAI_ALERT_TX_SUCCESS|TWAI_ALERT_TX_FAILED|TWAI_ALERT_TX_IDLE|
    TWAI_ALERT_RX_DATA|TWAI_ALERT_BUS_ERROR|TWAI_ALERT_BUS_OFF|
    TWAI_ALERT_RX_QUEUE_FULL|TWAI_ALERT_RX_FIFO_OVERRUN;
#if PROBE_REFERENCE
  twai_timing_config_t timing=TWAI_TIMING_CONFIG_25KBITS();
#else
  twai_timing_config_t timing=TWAI_TIMING_CONFIG_500KBITS();
#endif
  twai_filter_config_t filter=TWAI_FILTER_CONFIG_ACCEPT_ALL();
  auto err=twai_driver_install(&g,&timing,&filter);
  if(err==ESP_OK) err=twai_start();
  Serial.printf("PROBE init=%s\r\n",esp_err_to_name(err));
  if(err!=ESP_OK) { done=true; return; }
  active=true; report("BEFORE");
  twai_message_t f{};
  f.identifier=0x555; f.self=1; f.ss=PROBE_SINGLE_SHOT;
  f.data_length_code=PROBE_REFERENCE ? 1 : 8;
  for(unsigned i=0;i<f.data_length_code;++i) f.data[i]=uint8_t(i+1);
  err=twai_transmit(&f,0);
  Serial.printf("PROBE enqueue=%s (not completion)\r\n",esp_err_to_name(err));
  began=millis(); lastReport=began;
}
void loop() {
  delay(1); // Probe only: yield CPU while waiting; no unbounded driver wait.
  if(done || !active) return;
  uint32_t alerts=0;
  const auto a=twai_read_alerts(&alerts,0);
  if(a==ESP_OK) {
    seen|=alerts;
    if(alerts&TWAI_ALERT_TX_SUCCESS) ++completed;
  }
  twai_message_t f{};
  if(twai_receive(&f,0)==ESP_OK) {
    ++received;
    Serial.printf("PROBE RX id=%08lX ext=%u rtr=%u dlc=%u data=",
      (unsigned long)f.identifier,unsigned(f.extd),unsigned(f.rtr),f.data_length_code);
    for(unsigned i=0;i<f.data_length_code && i<8;++i) Serial.printf("%02X",f.data[i]);
    Serial.println();
  }
  const uint32_t now=millis();
  if(uint32_t(now-lastReport)>=500) { lastReport=now; report("WAIT"); }
  if(uint32_t(now-began)>=2500) {
    report("FINAL");
    Serial.printf("PROBE STOP=%s\r\n",esp_err_to_name(twai_stop()));
    active=false; done=true;
    // No recovery or further enqueue. RX alone never produces a PASS.
  }
}
