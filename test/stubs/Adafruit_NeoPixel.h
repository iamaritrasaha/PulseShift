// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
#include <vector>
#define NEO_GRB 1
#define NEO_KHZ800 2
inline std::vector<uint32_t> testPixels;
inline unsigned testLedWrites = 0;
class Adafruit_NeoPixel {
 public:
  Adafruit_NeoPixel(unsigned count,int,int) { testPixels.resize(count); }
  void begin() {} void setBrightness(unsigned) {}
  void clear() { for (auto& c:testPixels) c=0; }
  void show() { ++testLedWrites; }
  void setPixelColor(unsigned i,uint32_t color) { testPixels.at(i)=color; }
  uint32_t Color(uint8_t r,uint8_t g,uint8_t b) const { return (uint32_t(r)<<16)|(uint32_t(g)<<8)|b; }
};
