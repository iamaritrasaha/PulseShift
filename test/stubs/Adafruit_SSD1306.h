// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "Wire.h"
#include <string>
#define SSD1306_WHITE 1
#define SSD1306_SWITCHCAPVCC 2
#define HEX 16
inline unsigned testOledWrites=0;
inline int testOledBarWidth=0;
inline unsigned testOledBarFills=0;
inline std::string testOledText;
class Adafruit_SSD1306 {
 public:
  Adafruit_SSD1306(int,int,TwoWire*,int) {}
  bool begin(int,int,bool,bool) { return true; }
  void clearDisplay() { testOledText.clear(); testOledBarWidth=0; testOledBarFills=0; }
  void setTextColor(int) {} void setTextSize(int) {} void setCursor(int,int) {}
  void print(const char* s) { testOledText += s; }
  void print(unsigned n,int=10) { testOledText += std::to_string(n); }
  void drawRect(int,int,int,int,int) {}
  void fillRect(int x,int y,int width,int height,int) {
    if (x==1 && y==59 && height==4) { testOledBarWidth=width; ++testOledBarFills; }
  }
  void display() { ++testOledWrites; }
};
