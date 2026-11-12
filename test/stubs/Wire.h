// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include <stdint.h>
struct TwoWire { void begin(int,int) {} void setTimeOut(unsigned) {} void setClock(uint32_t) {} };
inline TwoWire Wire;
