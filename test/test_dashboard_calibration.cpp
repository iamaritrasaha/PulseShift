// Copyright (c) 2026 Aritra Saha. All rights reserved.
// Synthetic native fixture only. No test profile is compiled into ESP32 firmware.
#include "dashboard.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"OLED FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
static VehicleProfile fixture() {
  VehicleProfile p;
  p.identifier="synthetic-oled-only"; p.description="Artificial OLED regression calibration";
  p.status=ProfileStatus::VERIFIED; p.bitrate=250000; p.arbitrationId=0x612;
  p.requiredDlc=2; p.rpm={0,16,SignalOrder::LittleEndianLsb0};
  p.maximumRpm=12000; p.timeoutMs=300;
  // Deliberately distinct from BOTH the demo maximum and decoder validity limit.
  p.lights={1000,9000,10000,150};
  p.evidence={"SYNTHETIC native test only",true,true,true,true,true,true};
  return p;
}
static VehicleProfile selected=fixture();
// GNU ld redirects only the native selector call. Production selection and its
// fail-closed NS125 profile stay untouched, with no firmware test override.
extern "C" const VehicleProfile& __wrap__Z22selectedVehicleProfilev() { return selected; }
#endif

static CanFrame frame(uint16_t rpm,uint32_t at) {
  CanFrame f; f.id=0x612; f.dlc=f.payloadSize=2; f.timestampMs=at;
  f.data[0]=uint8_t(rpm); f.data[1]=uint8_t(rpm>>8); return f;
}
static RpmState state() {
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
  return RpmState(selected);
#else
  return RpmState(RpmMapping{true,0x612,false,2,0,true,1,1,12000});
#endif
}
static void blank() {
  CHECK(testOledBarFills==0 && testOledBarWidth==0);
  CHECK(testOledText.find("-- RPM")!=std::string::npos);
  for(auto pixel:testPixels) CHECK(pixel==0);
}
int main() {
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
  CHECK(vehicleProfileReady(selected));
#endif
  Dashboard dashboard; CHECK(dashboard.begin());
  auto rpm=state();
  const uint16_t maximum=
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
    10000;
#else
    config::maxRpm;
#endif
  CHECK(dashboard.shiftLight().config().rpmMax==maximum);
  uint32_t now=100;
  for(uint16_t value:{uint16_t(0),uint16_t(2500),uint16_t(5000),maximum,uint16_t(11000)}) {
    CHECK(rpm.ingest(frame(value,now))==DecodeResult::Valid);
    dashboard.update(rpm,now,true,true,rpm.timeoutMs(config::staleMs));
    const int expected=value>=maximum ? 126 : uint32_t(value)*126/maximum;
    CHECK(testOledBarFills==1 && testOledBarWidth==expected);
    now+=100;
  }
  const auto lastValid=now-100;
  rpm.expire(lastValid+rpm.timeoutMs(config::staleMs),rpm.timeoutMs(config::staleMs),config::staleMs);
  dashboard.update(rpm,lastValid+rpm.timeoutMs(config::staleMs),true,true,rpm.timeoutMs(config::staleMs));
  blank();
  now=2000;
  CHECK(rpm.ingest(frame(5000,now))==DecodeResult::Valid);
  dashboard.update(rpm,now,false,true,rpm.timeoutMs(config::staleMs)); blank();
  dashboard.update(rpm,now+100,true,false,rpm.timeoutMs(config::staleMs)); blank();
  now+=200;
  CHECK(rpm.ingest(frame(5000,now))==DecodeResult::Valid);
  dashboard.update(rpm,now,true,true,rpm.timeoutMs(config::staleMs));
  CHECK(testOledBarWidth==int(uint32_t(5000)*126/maximum) && testOledBarFills==1);
#if PULSESHIFT_MODE == PULSESHIFT_VEHICLE
  auto bad=frame(5000,now+100); bad.dlc=bad.payloadSize=1;
  CHECK(rpm.ingest(bad)==DecodeResult::WrongDlc);
  dashboard.update(rpm,now+100,true,true,selected.timeoutMs); blank();
  CHECK(rpm.ingest(frame(5000,now+200))==DecodeResult::Valid);
  dashboard.update(rpm,now+200,true,true,selected.timeoutMs);
  CHECK(testOledBarWidth==63 && testOledBarFills==1);
  // A realistic-looking mapping with missing evidence cannot drive the display.
  selected.status=ProfileStatus::UNVERIFIED;
  Dashboard disabled; CHECK(disabled.begin()); auto unconfigured=state();
  CHECK(!unconfigured.mapped());
  CHECK(unconfigured.ingest(frame(5000,3000))==DecodeResult::ProfileUnconfigured);
  disabled.update(unconfigured,3000,false,false,300); blank();
#endif
  std::printf("PASS OLED calibrated maximum, clamping, blanking and recovery mode=%d: %u checks\n",PULSESHIFT_MODE,checks);
}
