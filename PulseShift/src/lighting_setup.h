// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once

#include "config.h"
#include <stddef.h>
#include <stdint.h>

enum class RpmMappingMode : uint8_t { Automatic, Grouped, Individual, LegacyGrouped };
enum class AmbientStatus : uint8_t { Off, Playing, PausedByRpm, SuppressedBySafety, Waiting };

struct RpmMapping {
  RpmMappingMode mode = RpmMappingMode::LegacyGrouped;
  uint8_t activeLedCount = 0; // Automatic sizing uses every installed pixel.
  uint8_t entries = 0;
  uint16_t thresholds[config::maxPhysicalLeds]{};
  uint8_t groupSizes[config::maxPhysicalLeds]{};

  uint8_t activeCount(uint8_t installed) const {
    return activeLedCount == 0 || activeLedCount > installed ? installed : activeLedCount;
  }

  bool valid(uint8_t installed, uint16_t warningRpm) const {
    if (activeLedCount > installed || entries > config::maxPhysicalLeds) return false;
    if (mode == RpmMappingMode::LegacyGrouped) return entries == 0;
    const auto count = activeCount(installed);
    if (mode == RpmMappingMode::Automatic) {
      if (entries != 2) return false;
    } else if (mode == RpmMappingMode::Grouped) {
      uint16_t sum = 0;
      for (uint8_t i = 0; i < entries; ++i) {
        if (groupSizes[i] == 0 || groupSizes[i] > count) return false;
        sum += groupSizes[i];
      }
      if (sum != count) return false;
    } else if (mode == RpmMappingMode::Individual) {
      if (entries != count) return false;
    } else {
      return false;
    }
    for (uint8_t i = 0; i < entries; ++i) {
      if (thresholds[i] > warningRpm || (i && thresholds[i] < thresholds[i - 1])) return false;
    }
    return true;
  }

  void expand(uint16_t* output, uint8_t count, const uint16_t* legacy) const {
    switch (mode) {
      case RpmMappingMode::Automatic:
        for (uint8_t i = 0; i < count; ++i) {
          const uint32_t delta = thresholds[1] - thresholds[0];
          output[i] = count == 1 ? thresholds[0] :
              thresholds[0] + (delta * i + (count - 1) / 2) / (count - 1);
        }
        break;
      case RpmMappingMode::Grouped: {
        uint8_t pixel = 0;
        for (uint8_t group = 0; group < entries && pixel < count; ++group) {
          for (uint8_t i = 0; i < groupSizes[group] && pixel < count; ++i) {
            output[pixel++] = thresholds[group];
          }
        }
        while (pixel < count) output[pixel++] = thresholds[entries ? entries - 1 : 0];
        break;
      }
      case RpmMappingMode::Individual:
        for (uint8_t i = 0; i < count; ++i) {
          output[i] = thresholds[i < entries ? i : (entries ? entries - 1 : 0)];
        }
        break;
      case RpmMappingMode::LegacyGrouped:
        for (uint8_t i = 0; i < count; ++i) output[i] = legacy[i * config::rpmStages / count];
        break;
    }
  }
};

struct AmbientAnimation {
  bool enabled = false;
  uint8_t pattern = 1;
  uint16_t cycleMs = 2000;
};

// Version 1 setup payload, shared by snapshots, writes and schema-5 storage.
constexpr uint8_t LIGHTING_SETUP_VERSION = 1;
constexpr size_t LIGHTING_SETUP_HEADER = 15;
constexpr size_t LIGHTING_SETUP_MAX = LIGHTING_SETUP_HEADER + 3 * config::maxPhysicalLeds;
