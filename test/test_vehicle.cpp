// Copyright (c) 2026 Aritra Saha. All rights reserved.
// All IDs/calibration in this test are SYNTHETIC, never NS125 protocol evidence.
#include "rpm_decoder.h"
#include "dashboard_model.h"
#include "shift_light.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "VEHICLE FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
static unsigned checks = 0;
VehicleProfile fixture() {
  VehicleProfile p;
  p.identifier="synthetic-test-only"; p.description="Artificial protocol for regression tests";
  p.status=ProfileStatus::VERIFIED; p.bitrate=250000; p.arbitrationId=0x612;
  p.requiredDlc=8; p.rpm={16,16,SignalOrder::LittleEndianLsb0};
  p.scaleNumerator=1; p.scaleDenominator=4; p.offsetNumerator=-400;
  p.maximumRpm=10000; p.timeoutMs=300;
  p.validity={true,{7,1,SignalOrder::LittleEndianLsb0},1};
  p.multiplexer={true,{8,8,SignalOrder::LittleEndianLsb0},2};
  p.checksum={ChecksumKind::Sum8,0,7,7,0};
  p.counter={true,{32,4,SignalOrder::LittleEndianLsb0},16};
  p.lights={1500,6000,10000,150};
  p.evidence={"SYNTHETIC FIXTURE ONLY: not vehicle qualification",true,true,true,true,true,true};
  return p;
}
void checksum(CanFrame& f) { f.data[7]=0; for (unsigned i=0;i<7;++i) f.data[7]=uint8_t(f.data[7]+f.data[i]); }
CanFrame frame(uint16_t rpm=2430,uint8_t counter=0,uint32_t time=100) {
  CanFrame f; f.id=0x612; f.dlc=f.payloadSize=8; f.timestampMs=time;
  f.data[0]=0x81; f.data[1]=2; const uint16_t raw=uint16_t((rpm+100)*4);
  f.data[2]=uint8_t(raw); f.data[3]=uint8_t(raw>>8); f.data[4]=counter;
  f.data[6]=0xD3; checksum(f); return f;
}
bool rejectCustom(const CanFrame&) { return false; }
void decoding() {
  const auto p=fixture(); CHECK(vehicleProfileReady(p));
  CHECK(!vehicleProfileReady(selectedVehicleProfile()));
  CHECK(selectedVehicleProfile().bitrate==0 && selectedVehicleProfile().rpm.width==0);
  CHECK(selectedVehicleProfile().status==ProfileStatus::UNVERIFIED);
  for (auto s:{ProfileStatus::UNVERIFIED,ProfileStatus::CAPTURE_ONLY}) {
    auto q=p;q.status=s;VehicleDecoder d(q);uint16_t out=33;
    CHECK(d.decode(frame(),out)==DecodeResult::ProfileUnconfigured && out==33);
  }
  for (unsigned missing=0;missing<7;++missing) {
    auto q=p;
    if(missing==0)q.evidence.reference="";
    if(missing==1)q.evidence.repeatedCaptures=false;
    if(missing==2)q.evidence.independentRpm=false;
    if(missing==3)q.evidence.electricalQualification=false;
    if(missing==4)q.evidence.shiftCalibrationVerified=false;
    if(missing==5)q.evidence.passiveBroadcastVerified=false;
    if(missing==6)q.evidence.independentHoldoutVerified=false;
    CHECK(!vehicleProfileReady(q));
    RpmState blocked(q); CHECK(!blocked.mapped());
    CHECK(blocked.ingest(frame())==DecodeResult::ProfileUnconfigured);
    CHECK(!blocked.fresh(100,q.timeoutMs));
  }
  auto expect=[&](CanFrame f,DecodeResult result) {
    VehicleDecoder d(p);uint16_t out=33;CHECK(d.decode(f,out)==result);
    if(result!=DecodeResult::Valid) CHECK(out==33);
  };
  auto f=frame();expect(f,DecodeResult::Valid);
  f.id=0x613;expect(f,DecodeResult::WrongId);
  f=frame();f.extended=true;expect(f,DecodeResult::WrongId);
  f=frame();f.id=0x800;expect(f,DecodeResult::Malformed);
  f=frame();f.remote=true;f.payloadSize=0;expect(f,DecodeResult::Remote);
  for(unsigned n=0;n<=9;++n){f=frame();f.dlc=uint8_t(n);expect(f,n==8?DecodeResult::Valid:n==9?DecodeResult::Malformed:DecodeResult::WrongDlc);}
  f=frame();f.payloadSize=7;expect(f,DecodeResult::Malformed);
  f=frame();f.data[0]=0;checksum(f);expect(f,DecodeResult::InvalidValue);
  f=frame();f.data[1]=3;checksum(f);expect(f,DecodeResult::WrongMux);
  f=frame();f.data[2]^=1;expect(f,DecodeResult::BadChecksum);
  f=frame(10001);expect(f,DecodeResult::OutOfRange);
  f=frame();f.data[2]=0;f.data[3]=0;checksum(f);expect(f,DecodeResult::OutOfRange);
  auto big=p;big.counter.enabled=false;big.rpm.order=SignalOrder::BigEndianMsb0;
  f=frame();const auto low=f.data[2];f.data[2]=f.data[3];f.data[3]=low;checksum(f);
  VehicleDecoder be(big);uint16_t beRpm=0;CHECK(be.decode(f,beRpm)==DecodeResult::Valid && beRpm==2430);
  auto q=p;q.additionalValidation=rejectCustom;VehicleDecoder custom(q);uint16_t out=42;
  CHECK(custom.decode(frame(),out)==DecodeResult::RuleRejected && out==42);
  VehicleDecoder d(p);
  for (uint16_t rpm:{uint16_t(0),uint16_t(1),uint16_t(9999),uint16_t(10000)}) {
    d=VehicleDecoder(p);CHECK(d.decode(frame(rpm),out)==DecodeResult::Valid && out==rpm);
  }
  q=p;q.checksum={ChecksumKind::Xor8,0,7,7,0x5A};q.counter.enabled=false;
  f=frame();f.data[7]=0x5A;for(unsigned i=0;i<7;++i)f.data[7]^=f.data[i];
  VehicleDecoder xd(q);CHECK(xd.decode(f,out)==DecodeResult::Valid && out==2430);
  f.data[7]^=1;CHECK(xd.decode(f,out)==DecodeResult::BadChecksum);
}
void bitfieldsAndMath() {
  CanFrame f;f.id=1;f.dlc=f.payloadSize=8;
  for(unsigned i=0;i<8;++i)f.data[i]=uint8_t(0xA5+i*7);
  for (unsigned start=0;start<64;++start) for(unsigned width=1;width<=32 && start+width<=64;++width) {
    for (auto order:{SignalOrder::LittleEndianLsb0,SignalOrder::BigEndianMsb0}) {
      CanSignal s{uint8_t(start),uint8_t(width),order}; uint32_t out=0,expected=0;
      for(unsigned i=0;i<width;++i) {
        const unsigned index=order==SignalOrder::LittleEndianLsb0?start+i:start+width-1-i;
        const unsigned bit=order==SignalOrder::LittleEndianLsb0?index%8:7-index%8;
        expected |= uint32_t((f.data[index/8]>>bit)&1U)<<i;
      }
      CHECK(extractSignal(f,s,out)); CHECK(out==expected);
    }
  }
  uint32_t raw=123;CHECK(!extractSignal(f,{63,2,SignalOrder::LittleEndianLsb0},raw) && raw==123);
  CHECK(!validSignal({0,0,SignalOrder::LittleEndianLsb0},8));
  auto p=fixture();p.counter.enabled=false;p.checksum.kind=ChecksumKind::None;
  p.validity.enabled=p.multiplexer.enabled=false;p.rpm={0,32,SignalOrder::LittleEndianLsb0};
  p.scaleNumerator=2147483647;p.offsetNumerator=2147483647;
  VehicleDecoder d(p);for(auto& b:f.data)b=255;f.id=p.arbitrationId;uint16_t out=77;
  CHECK(d.decode(f,out)==DecodeResult::OutOfRange && out==77);
  p.scaleNumerator=1;p.scaleDenominator=2;p.offsetNumerator=0;p.maximumRpm=10000;
  f.data[0]=0x21;f.data[1]=0x4E;f.data[2]=f.data[3]=0; // 20001 / 2 = 10000.5
  d=VehicleDecoder(p);CHECK(d.decode(f,out)==DecodeResult::OutOfRange);
  p.extended=true;p.arbitrationId=0x1ABCDE;f.extended=true;f.id=p.arbitrationId;f.data[0]=0;f.data[1]=0;
  d=VehicleDecoder(p);CHECK(d.decode(f,out)==DecodeResult::Valid && out==0);
  for(unsigned variant=0;variant<10;++variant) {
    p=fixture();
    switch(variant) {
      case 0:p.scaleDenominator=0;break;case 1:p.rpm.width=33;break;
      case 2:p.rpm.startBit=63;break;case 3:p.requiredDlc=9;break;
      case 4:p.checksum.checksumByte=0;break;case 5:p.counter.modulus=17;break;
      case 6:p.timeoutMs=0x80000000U;break;case 7:p.lights.shift=0;break;
      case 8:p.scaleNumerator=0xFFFFFFFFU;break;case 9:p.bitrate=12345;break;
    }
    CHECK(!vehicleProfileReady(p));
  }
}
void stateAndLeds() {
  auto p=fixture();RpmState s(p);uint32_t now=0xFFFFFF80U;
  auto f=frame(4000,14,now);CHECK(s.ingest(f)==DecodeResult::Valid);
  CHECK(s.fresh(now+299,p.timeoutMs));CHECK(!s.fresh(now+300,p.timeoutMs));
  f=frame(4000,15,now+100);CHECK(s.ingest(f)==DecodeResult::Valid);
  f=frame(4000,0,now+200);CHECK(s.ingest(f)==DecodeResult::Valid);
  f=frame(4000,3,now+210);CHECK(s.ingest(f)==DecodeResult::BadCounter);CHECK(!s.fresh(now+210,p.timeoutMs));
  CHECK(viewStatus(s,now+210,p.timeoutMs,true)==ViewStatus::Invalid);
  f=frame(4000,4,now+220);CHECK(s.ingest(f)==DecodeResult::Valid);
  auto unrelated=f;unrelated.id++;unrelated.timestampMs=now+450;
  CHECK(s.ingest(unrelated)==DecodeResult::WrongId);
  CHECK(s.busFresh(now+550,p.timeoutMs));CHECK(!s.fresh(now+550,p.timeoutMs));
  CHECK(viewStatus(s,now+550,p.timeoutMs,true)==ViewStatus::Stale);
  f=frame(6000,12,now+650);CHECK(s.ingest(f)==DecodeResult::Valid); // stale resync
  ShiftLightConfig c;c.startupAnimationEnabled=false;c.smoothTransitions=false;
  c.rpmStart=p.lights.start;c.rpmShift=p.lights.shift;c.rpmMax=p.lights.maximum;
  c.hysteresisRpm=p.lights.hysteresis;ShiftLightEngine engine(c);
  engine.reset(now);CHECK(engine.update(now+650,s.rpm(),viewStatus(s,now+650,p.timeoutMs,true)));
  bool lit=false;for(unsigned i=0;i<6;++i){auto pixel=engine.getPixel(i);lit|=pixel.r||pixel.g||pixel.b;}CHECK(lit);
  engine.update(now+1000,0,viewStatus(s,now+1000,p.timeoutMs,true));
  for(unsigned i=0;i<6;++i){auto pixel=engine.getPixel(i);CHECK(!pixel.r&&!pixel.g&&!pixel.b);}
  f=frame(3500,0,now+1100);CHECK(s.ingest(f)==DecodeResult::Valid);
  engine.update(now+1100,s.rpm(),viewStatus(s,now+1100,p.timeoutMs,true));CHECK(engine.getPixel(0).g>0);
  engine.update(now+1150,s.rpm(),viewStatus(s,now+1150,p.timeoutMs,false));
  for(unsigned i=0;i<6;++i){auto pixel=engine.getPixel(i);CHECK(!pixel.r&&!pixel.g&&!pixel.b);}
  f=frame(0,1,now+1200);CHECK(s.ingest(f)==DecodeResult::Valid && s.rpm()==0);
  CHECK(s.fresh(now+1200,p.timeoutMs));
  RpmState lapped(p);CHECK(lapped.ingest(frame(3500,5,100))==DecodeResult::Valid);
  lapped.expire(1100,p.timeoutMs,1000);
  CHECK(!lapped.fresh(100,p.timeoutMs) && !lapped.busFresh(100,1000));
  // Same timer values after a complete lap cannot revive an expired reading.
  CHECK(lapped.ingest(frame(0,12,120))==DecodeResult::Valid);
  CHECK(lapped.fresh(120,p.timeoutMs) && lapped.rpm()==0);
  RpmState unconfigured(selectedVehicleProfile());CHECK(!unconfigured.mapped());
  CHECK(viewStatus(unconfigured,0,1000,false,false)==ViewStatus::Unconfigured);
}
void replayFixture() {
  std::ifstream input("test/fixtures/synthetic_vehicle.csv");CHECK(bool(input));
  std::string line;std::getline(input,line);RpmState state(fixture());unsigned count=0;
  ShiftLightConfig cfg;cfg.startupAnimationEnabled=false;
  cfg.rpmStart=1500;cfg.rpmShift=6000;cfg.rpmMax=10000;
  ShiftLightEngine engine(cfg);engine.reset(0);
  while(std::getline(input,line)) {
    std::stringstream row(line);std::string fields[6];
    for(auto& value:fields)CHECK(bool(std::getline(row,value,',')));
    CanFrame f;f.timestampMs=uint32_t(std::stoul(fields[0]));f.id=uint32_t(std::stoul(fields[1],nullptr,16));
    f.extended=fields[2]=="1";f.dlc=f.payloadSize=uint8_t(std::stoul(fields[3]));
    CHECK(fields[4].size()==size_t(f.dlc)*2);
    for(unsigned i=0;i<f.dlc;++i)f.data[i]=uint8_t(std::stoul(fields[4].substr(i*2,2),nullptr,16));
    CHECK(state.ingest(f)==DecodeResult::Valid);CHECK(state.rpm()==std::stoul(fields[5]));
    engine.update(f.timestampMs,state.rpm(),viewStatus(state,f.timestampMs,300,true));
    if(state.rpm()==0)for(unsigned i=0;i<6;++i){auto c=engine.getPixel(i);CHECK(!c.r&&!c.g&&!c.b);}
    ++count;
  }
  CHECK(count==84);
}
int main() { replayFixture();decoding();bitfieldsAndMath();stateAndLeds();std::printf("PASS vehicle profile, bitfields, rules, counter recovery, state and LED path: %u checks\n",checks); }
