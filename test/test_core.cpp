#include "rpm_decoder.h"
#include "dashboard_model.h"
#include "tx_tracker.h"
#include "simulated_ecu.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
unsigned assertions = 0, cases = 0;
#define CHECK(expr) do { ++assertions; if (!(expr)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); std::exit(1); } } while(0)
void pass(const char* name) { ++cases; std::printf("PASS %s\n",name); }
CanFrame frame(uint16_t raw=2430) {
  auto f=demonstrationFrame(100,0); f.data[0]=uint8_t(raw); f.data[1]=uint8_t(raw>>8); return f;
}
void decoding() {
  auto m=demonstrationMapping(); auto f=frame(); uint16_t rpm=42;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  pass("valid LE CAN frame");
  f.id=0x301; CHECK(decodeRpm(f,m,rpm)==DecodeResult::WrongId);
  f.id=0x800; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Malformed);
  f=frame(); f.extended=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::WrongId);
  m.extended=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid);
  f.id=m.id=0x1FFFFFFF; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid);
  f.id=0x20000000; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Malformed);
  pass("identifier width and standard/extended distinction");
  m=demonstrationMapping(); f=frame();
  for(unsigned dlc=0;dlc<256;++dlc) {
    f.dlc=uint8_t(dlc); rpm=42;
    const auto r=decodeRpm(f,m,rpm);
    CHECK(r==(dlc==8 ? DecodeResult::Valid : dlc>8 ? DecodeResult::Malformed : DecodeResult::WrongDlc));
    CHECK(rpm==(dlc==8 ? 2430 : 42));
  }
  pass("all DLC values and output preservation on rejection");
  f=frame(); f.remote=true; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Remote);
  m.enabled=false; f.remote=false; CHECK(decodeRpm(f,m,rpm)==DecodeResult::Disabled);
  pass("RTR rejection and disabled bike mapping");
  m=demonstrationMapping(); m.littleEndian=false; f=frame(); f.data[0]=0x09; f.data[1]=0x7E;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  m.offset=6; f.data[6]=0x09; f.data[7]=0x7E;
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==2430);
  m.offset=7; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m.offset=255; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m.offset=0; m.denominator=0; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  m=demonstrationMapping(); m.id=0x800; CHECK(decodeRpm(f,m,rpm)==DecodeResult::BadMapping);
  pass("BE, offsets, mapping bounds, zero divisor");
  m=demonstrationMapping(); m.numerator=3; m.denominator=2;
  f=frame(3); CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==4);
  f=frame(4334); CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange);
  m.numerator=1; m.denominator=2; f=frame(13001);
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange); // 6500.5 must not round into range
  m.numerator=0xFFFFFFFF; m.denominator=0xFFFFFFFF; f=frame(6500);
  CHECK(decodeRpm(f,m,rpm)==DecodeResult::Valid); CHECK(rpm==6500);
  m.denominator=1; CHECK(decodeRpm(f,m,rpm)==DecodeResult::OutOfRange);
  pass("rational conversion, rounding and overflow safety");
  m=demonstrationMapping();
  for(unsigned raw=0;raw<=65535;++raw) {
    f=frame(uint16_t(raw)); rpm=42; const auto result=decodeRpm(f,m,rpm);
    CHECK(result==(raw<=6500 ? DecodeResult::Valid : DecodeResult::OutOfRange));
    CHECK(rpm==(raw<=6500 ? raw : 42));
  }
  pass("exhaustive 16-bit RPM range including zero and limits");
}
void state() {
  RpmState s(demonstrationMapping());
  CHECK(!s.fresh(0,1000)); CHECK(!s.hasValue());
  CHECK(viewStatus(s,0,1000,true)==ViewStatus::Disconnected);
  auto f=frame(0); f.timestampMs=0; CHECK(s.ingest(f)==DecodeResult::Valid);
  CHECK(s.fresh(999,1000)); CHECK(s.rpm()==0); CHECK(!s.fresh(1000,1000));
  CHECK(viewStatus(s,1000,1000,true)==ViewStatus::Stale);
  f=frame(6501); f.timestampMs=1100; s.ingest(f);
  CHECK(!s.fresh(1100,1000)); CHECK(s.rpm()==0); CHECK(s.busFresh(1100,1000));
  f=frame(4000); f.timestampMs=1200; s.ingest(f);
  CHECK(s.fresh(1200,1000)); CHECK(s.rpm()==4000); CHECK(s.accepted()==2); CHECK(s.rejected()==1);
  CHECK(viewStatus(s,1200,1000,false)==ViewStatus::Fault);
  CHECK(viewStatus(s,1200,1000,true,false)==ViewStatus::Unconfigured);
  pass("missing, valid zero, stale boundary, invalid non-refresh, recovery and faults");
  RpmState wrap(demonstrationMapping()); f.timestampMs=0xFFFFFFF0; wrap.ingest(f);
  CHECK(wrap.fresh(0x20,1000)); CHECK(!wrap.fresh(0x400,1000));
  pass("millis rollover");
  RpmState bike(RpmMapping{}); f.timestampMs=20; bike.ingest(f);
  CHECK(!bike.hasValue()); CHECK(viewStatus(bike,20,1000,true)==ViewStatus::Unmapped);
  CHECK(viewStatus(bike,1020,1000,true)==ViewStatus::Disconnected);
  RpmState invalid(demonstrationMapping()); f.dlc=1; invalid.ingest(f);
  CHECK(viewStatus(invalid,20,1000,true)==ViewStatus::Invalid);
  pass("unmapped, invalid and disconnected states");
}
void leds() {
  CHECK(litLedCount(0,6500,6,true)==0);
  for(unsigned n=1;n<=6;++n) {
    unsigned threshold=(6500*n+5)/6;
    CHECK(litLedCount(threshold-1,6500,6,true)==n-1);
    CHECK(litLedCount(threshold,6500,6,true)==n);
  }
  CHECK(litLedCount(65535,6500,6,true)==6);
  CHECK(litLedCount(6500,6500,6,false)==0);
  CHECK(litLedCount(6500,0,6,true)==0);
  pass("six LED thresholds, clamping and stale/off behavior");
}
void tx() {
  auto f=frame(); TxTracker t;
  CHECK(t.begin(f,0)); CHECK(!t.begin(f,0)); CHECK(t.onReceive(f)); CHECK(!t.finish(1)); CHECK(!t.finish(0));
  CHECK(!t.timedOut(1499,1500)); CHECK(t.timedOut(1500,1500)); CHECK(t.halt());
  CHECK(!t.halt()); CHECK(!t.timedOut(1600,1500)); CHECK(!t.begin(f,2000)); CHECK(!t.onTxSuccess());
  pass("original RX-without-TX regression, single timeout, no queue flooding");
  for(unsigned order=0;order<2;++order) {
    TxTracker ok; CHECK(ok.begin(f,0));
    if(order==0) { CHECK(ok.onReceive(f)); CHECK(ok.onTxSuccess()); }
    else { CHECK(ok.onTxSuccess()); CHECK(ok.onReceive(f)); }
    CHECK(!ok.finish(1)); CHECK(ok.finish(0)); CHECK(!ok.pending()); CHECK(ok.begin(f,100));
  }
  TxTracker mismatch; CHECK(mismatch.begin(f,0)); f.data[4]^=1;
  CHECK(!mismatch.onReceive(f)); f.data[4]^=1;
  CHECK(mismatch.onReceive(f)); CHECK(!mismatch.onReceive(f));
  pass("RX/TX ordering, driver pending requirement, sequence mismatch and duplicates");
}
void replay() {
  ReplayTransport replay; RpmState s(demonstrationMapping()); CanFrame f;
  for(uint32_t now=0;now<=20000;now+=100) {
    const bool got=replay.receive(now,f);
    if(got) s.ingest(f);
    if(now==100) CHECK(s.fresh(now,1000));
    if(now==13500) CHECK(!s.fresh(now,1000));
    if(now==16000) { CHECK(!got); CHECK(!s.busFresh(now,1000)); }
    if(now==17000) CHECK(s.fresh(now,1000));
  }
  pass("full-frame replay, invalid interval, silence and resumed frames");
}
}
int main() {
  decoding(); state(); leds(); tx(); replay();
  std::printf("ALL PASS: %u cases, %u assertions\n",cases,assertions); return 0;
}
