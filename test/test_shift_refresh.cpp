// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "shift_light.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr,"FAIL refresh line %d: %s\n",__LINE__,#e); std::exit(1); } } while(0)
void blanking() {
  for (const auto status : {ViewStatus::Fault, ViewStatus::Unconfigured,
       ViewStatus::Disconnected, ViewStatus::Unmapped, ViewStatus::Invalid, ViewStatus::Stale}) {
    for (const uint32_t base : {100u, UINT32_MAX-1}) {
      for (const bool startup : {false,true}) {
        ShiftLightConfig c; c.animationIntervalMs=250; c.startupAnimationEnabled=startup;
        ShiftLightEngine e(c); e.reset(base);
        CHECK(e.update(base,6000,ViewStatus::Live));
        CHECK(e.update(base+2,0,status));
        CHECK(!e.isAlertActive());
        for (unsigned i=0;i<6;++i) CHECK(e.getPixel(i)==RgbColor());
        CHECK(!e.update(base+3,0,status));
        CHECK(e.update(base+4,6000,ViewStatus::Live));
        bool lit=false;for (unsigned i=0;i<6;++i) lit=lit || e.getPixel(i)!=RgbColor();
        CHECK(lit);
      }
    }
  }
  std::puts("PASS immediate non-live blanking, startup/alert, rollover and recovery");
}
void refresh() {
  ShiftLightConfig c;c.startupAnimationEnabled=false;c.animationIntervalMs=250;
  ShiftLightEngine e(c);e.reset(100);
  CHECK(e.update(100,6000,ViewStatus::Live));
  const auto before=e.getPixel(0);
  for (uint32_t now=101;now<350;++now) CHECK(!e.update(now,6000,ViewStatus::Live));
  e.setMasterBrightness(10);
  CHECK(!e.update(349,6000,ViewStatus::Live));CHECK(e.getPixel(0)==before);
  CHECK(e.update(350,6000,ViewStatus::Live));CHECK(e.getPixel(0)!=before);
  e.setStyle(ShiftLightStyle::Minimal);
  CHECK(!e.update(351,3000,ViewStatus::Live));
  CHECK(e.update(600,3000,ViewStatus::Live));
  CHECK(!e.update(601,3000,ViewStatus::Live));
  std::puts("PASS throttled updates stay quiet and pending visual preferences render");
}
int main(int argc,char** argv) {
  if (argc==1 || std::strcmp(argv[1],"blanking")==0) blanking();
  if (argc==1 || std::strcmp(argv[1],"refresh")==0) refresh();
}
