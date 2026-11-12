// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "can_frame.h"
enum class ProfileStatus { UNVERIFIED, CAPTURE_ONLY, VERIFIED };
// LE: startBit is an LSB0 offset; BE: startBit is an MSB0 network-stream offset.
// BE is NOT a DBC Motorola start-bit number. Only unsigned fields, 1..32 bits.
enum class SignalOrder { LittleEndianLsb0, BigEndianMsb0 };
struct CanSignal {
  uint8_t startBit = 0, width = 0;
  SignalOrder order = SignalOrder::LittleEndianLsb0;
};
struct MatchRule { bool enabled = false; CanSignal signal{}; uint32_t expected = 0; };
enum class ChecksumKind { None, Sum8, Xor8 };
struct ChecksumRule {
  ChecksumKind kind = ChecksumKind::None;
  uint8_t firstByte = 0, byteCount = 0, checksumByte = 0, seed = 0;
};
struct CounterRule { bool enabled = false; CanSignal signal{}; uint32_t modulus = 0; };
struct ShiftCalibration { uint16_t start = 0, shift = 0, maximum = 0, hysteresis = 0; };
struct ProfileEvidence {
  const char* reference = ""; // Reviewed service/capture/measurement references.
  bool repeatedCaptures = false, independentRpm = false, electricalQualification = false;
  bool shiftCalibrationVerified = false;
  // Reviewed passive traffic without a scanner, plus unseen-session validation.
  // Missing evidence keeps even a numerically complete profile disabled.
  bool passiveBroadcastVerified = false, independentHoldoutVerified = false;
};
struct VehicleProfile {
  const char* identifier = "";
  const char* description = "";
  ProfileStatus status = ProfileStatus::UNVERIFIED;
  uint32_t bitrate = 0, arbitrationId = 0;
  bool extended = false;
  uint8_t requiredDlc = 0;
  CanSignal rpm{};
  // RPM = (raw * scaleNumerator + offsetNumerator) / scaleDenominator.
  // Positive numerator <= INT32_MAX; signed offset in common-denominator units.
  uint32_t scaleNumerator = 1, scaleDenominator = 1;
  int32_t offsetNumerator = 0;
  uint16_t minimumRpm = 0, maximumRpm = 0;
  uint32_t timeoutMs = 0;
  MatchRule validity{}, multiplexer{};
  ChecksumRule checksum{};
  CounterRule counter{};
  // Explicit extension point for a reviewed proprietary rule. No generic CRC claim.
  bool (*additionalValidation)(const CanFrame&) = nullptr;
  ShiftCalibration lights{};
  ProfileEvidence evidence{};
};
bool validSignal(const CanSignal&, uint8_t dlc);
bool extractSignal(const CanFrame&, const CanSignal&, uint32_t& raw);
bool validProfileDefinition(const VehicleProfile&);
bool vehicleProfileReady(const VehicleProfile&);
const char* profileStatusName(ProfileStatus);
const VehicleProfile& selectedVehicleProfile(); // No synthetic profile in firmware.
