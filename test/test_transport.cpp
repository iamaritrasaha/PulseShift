// Copyright (c) 2026 Aritra Saha. All rights reserved.
// A driver double checks application safety choices, not physical CAN behavior.
#include <Arduino.h>
#include "can_transport.h"
#include "dashboard_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"TRANSPORT FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
unsigned checks=0,starts=0,stops=0,installs=0;
std::vector<int> routing;
twai_status_info_t status;
twai_message_t next;
bool available=false;
uint32_t alerts=0;
int installResult=ESP_OK,rxResult=ESP_OK,alertResult=ESP_OK;
int gpio_reset_pin(gpio_num_t p){CHECK(p==17);routing.push_back(1);return ESP_OK;}
int gpio_set_direction(gpio_num_t p,int m){CHECK(p==17 && m==GPIO_MODE_INPUT);routing.push_back(2);return ESP_OK;}
int gpio_set_pull_mode(gpio_num_t p,int m){CHECK(p==17 && m==GPIO_PULLUP_ONLY);routing.push_back(3);return ESP_OK;}
int twai_driver_install(const twai_general_config_t* g,const twai_timing_config_t* t,const twai_filter_config_t*) {
  ++installs;CHECK(g->mode==TWAI_MODE_LISTEN_ONLY);CHECK(g->tx_io==17 && g->rx_io==16);
  CHECK(g->tx_queue_len==0 && g->rx_queue_len==64);CHECK(t->bitrate==500000);
  CHECK(!(g->alerts_enabled & (TWAI_ALERT_TX_SUCCESS|TWAI_ALERT_TX_FAILED|TWAI_ALERT_TX_IDLE)));
  return installResult;
}
int twai_start(){CHECK(routing==std::vector<int>({1,2,3}));++starts;status.state=TWAI_STATE_RUNNING;return ESP_OK;}
int twai_stop(){++stops;status.state=TWAI_STATE_STOPPED;return ESP_OK;}
int twai_read_alerts(uint32_t* a,uint32_t timeout){CHECK(timeout==0);*a=alerts;alerts=0;return alertResult;}
int twai_get_status_info(twai_status_info_t* s){*s=status;return ESP_OK;}
int twai_receive(twai_message_t* f,uint32_t timeout){CHECK(timeout==0);if(rxResult!=ESP_OK)return rxResult;if(!available)return ESP_ERR_TIMEOUT;*f=next;available=false;return ESP_OK;}
void reset() { routing.clear();status={};next={};available=false;alerts=0;installResult=rxResult=alertResult=ESP_OK;Serial.output.clear();Serial.capacity=0; }
int main() {
  reset();FrameLog disabledLog;CanTransport disabled(0,&disabledLog);
  CHECK(!disabled.configured() && !disabled.begin());CHECK(installs==0);
  reset();CanTransport unknown(333333);CHECK(!unknown.begin() && !unknown.healthy());CHECK(installs==0);
  for(uint32_t terminal:{TWAI_ALERT_BUS_OFF,TWAI_ALERT_RX_QUEUE_FULL,TWAI_ALERT_RX_FIFO_OVERRUN}) {
    reset();FrameLog log;CanTransport can(500000,&log);CHECK(can.begin());CHECK(can.healthy());const auto began=starts;CHECK(can.begin() && starts==began);
    next.identifier=0x123;next.data_length_code=2;next.data[0]=0xAA;next.data[1]=0xBB;available=true;
    CanFrame frame;CHECK(can.receive(frame,0xFFFFFFF0));CHECK(frame.payloadSize==2 && frame.timestampMs==0xFFFFFFF0 && frame.data[1]==0xBB);
    CHECK(can.acceptedFrames()==1);
    log.push(frame,DecodeResult::Disabled);
    next.data_length_code=8;next.rtr=true;available=true;
    CHECK(can.receive(frame,20));CHECK(frame.remote && frame.payloadSize==0 && validEnvelope(frame));
    alerts=TWAI_ALERT_BUS_ERROR;can.poll(1000);CHECK(can.healthy()); // Report-only isolated error.
    const auto before=stops;alerts=terminal;can.poll(1010);CHECK(!can.healthy() && stops==before+1);
    for(unsigned i=0;i<1000;++i) { can.poll(2000+i); }
    CHECK(stops==before+1);
    CHECK(!can.receive(frame,3000));CHECK(!can.begin() && stops==before+1);
    // No UART space: polling/status must return and record loss rather than write.
    Serial.output.clear();can.diagnostics();CHECK(Serial.output.empty());
    Serial.capacity=4096;for(unsigned i=0;i<64;++i)log.pump();
    CHECK(Serial.output.find("CAN HALTED:")!=std::string::npos);
    CHECK(Serial.output.find("missed=")!=std::string::npos);
    CHECK(Serial.output.find("acceptedFrames=")!=std::string::npos);
  }
  reset();FrameLog log;CanTransport can(500000,&log);CHECK(can.begin());
  rxResult=ESP_FAIL;CanFrame frame;CHECK(!can.receive(frame,10) && !can.healthy());
  reset();CanTransport stopped(500000,&log);CHECK(stopped.begin());status.state=TWAI_STATE_STOPPED;stopped.poll(20);CHECK(!stopped.healthy());
  reset();CanTransport pending(500000,&log);CHECK(pending.begin());status.msgs_to_tx=1;pending.poll(20);CHECK(!pending.healthy());
  reset();CanTransport missed(500000,&log);CHECK(missed.begin());status.rx_missed_count=1;missed.poll(20);CHECK(!missed.healthy());
  reset();CanTransport alertFailure(500000,&log);CHECK(alertFailure.begin());alertResult=ESP_FAIL;alertFailure.poll(20);CHECK(!alertFailure.healthy());
  reset();installResult=ESP_FAIL;const auto startCount=starts;CanTransport failed(500000,&log);CHECK(!failed.begin() && starts==startCount);
  // A priority fault record survives a full frame queue with explicit loss count.
  Serial.output.clear();Serial.capacity=0;FrameLog full;
  for(unsigned i=0;i<64;++i)full.push(frame,DecodeResult::Disabled);
  full.status("CAN HALTED: test");CHECK(full.dropped()==1);CHECK(Serial.output.empty());
  Serial.capacity=4096;for(unsigned i=0;i<64;++i)full.pump();
  CHECK(Serial.output.find("CAN HALTED: test\r\n")!=std::string::npos);
  char huge[200];memset(huge,'x',sizeof(huge)-1);huge[199]=0;const auto drops=full.dropped();
  full.status("%s",huge);CHECK(full.dropped()==drops+1);
  std::printf("PASS receive-only routing, no TX linkage, driver errors/overflow, bounded logging: %u checks\n",checks);
}
