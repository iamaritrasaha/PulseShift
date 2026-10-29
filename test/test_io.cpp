// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include <Arduino.h>
#include "frame_log.h"
#include "dashboard.h"
#include "simulated_ecu.h"
#include <cstdlib>
#include <cstdio>
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr,"FAIL IO line %d: %s\n",__LINE__,#e); std::exit(1); } } while(0)
int main() {
  FrameLog logger;
  auto f=demonstrationFrame(UINT32_MAX,UINT32_MAX);
  f.id=0x1FFFFFFF; f.extended=true; f.dlc=255;
  Serial.capacity=0;
  for(unsigned i=0;i<10000;++i) { logger.push(f,DecodeResult::Malformed); logger.pump(); }
#if PULSESHIFT_SERIAL_DIAGNOSTICS
  CHECK(logger.dropped()==10000-64); CHECK(Serial.output.empty());
  Serial.capacity=4096;
  for(unsigned i=0;i<64;++i) logger.pump();
  const auto size=Serial.output.size(); logger.pump(); CHECK(Serial.output.size()==size);
  CHECK(Serial.output.find("dlc=255")!=std::string::npos);
  CHECK(Serial.output.find("MALFORMED\r\n")!=std::string::npos);
  for(size_t i=0;i<Serial.output.size();++i) if(Serial.output[i]=='\n') CHECK(i>0 && Serial.output[i-1]=='\r');
  logger.push(f,DecodeResult::Remote); logger.pump();
  CHECK(Serial.output.size()>size); // queue resumes after backpressure is removed
#else
  CHECK(logger.dropped()==0); CHECK(Serial.output.empty());
#endif
#if PULSESHIFT_OLED_ENABLED
  Dashboard dashboard; CHECK(dashboard.begin());
  RpmState rpm(demonstrationMapping());
  auto frame=demonstrationFrame(100,0); rpm.ingest(frame);
  dashboard.update(rpm,100,true,true); const auto first=testOledWrites;
  const auto ledFirst=testLedWrites;
  dashboard.update(rpm,101,true,true);
  CHECK(testLedWrites==ledFirst); // unchanged throttled animation must not show again
  CHECK(first==1);
  for(unsigned now=101;now<200;++now) {
    frame.timestampMs=now; frame.data[0]=uint8_t(now); rpm.ingest(frame);
    dashboard.update(rpm,now,true,true);
  }
  CHECK(testOledWrites==first); // changed RPM cannot bypass the 100ms cap
  dashboard.update(rpm,200,true,true); CHECK(testOledWrites==first+1);
  dashboard.update(rpm,300,true,true); CHECK(testOledWrites==first+1); // unchanged display stays quiet
  dashboard.update(rpm,1300,true,true);
  CHECK(testOledText.find("-- RPM")!=std::string::npos);
  for(auto c:testPixels) CHECK(c==0);
#endif
  std::puts("PASS UART backpressure/overflow/recovery, CRLF, serial-off behavior, OLED refresh cap and stale output");
}
