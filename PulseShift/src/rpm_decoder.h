// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "can_frame.h"
#include "vehicle_profile.h"
enum class DecodeResult { Valid, Disabled, Malformed, Remote, WrongId, WrongDlc, BadMapping, OutOfRange, ProfileUnconfigured, InvalidValue, WrongMux, BadChecksum, BadCounter, RuleRejected };
struct RpmMapping {
  bool enabled = false;
  uint32_t id = 0;
  bool extended = false;
  uint8_t dlc = 8, offset = 0;
  bool littleEndian = true;
  uint32_t numerator = 1, denominator = 1;
  uint16_t maxRpm = 6500;
};
DecodeResult decodeRpm(const CanFrame&, const RpmMapping&, uint16_t& rpm);
const char* decodeName(DecodeResult);
class VehicleDecoder {
 public:
  explicit VehicleDecoder(VehicleProfile profile) : profile_(profile) {}
  DecodeResult decode(const CanFrame&, uint16_t&);
  void resetContinuity() { hasCounter_ = false; }
  bool ready() const { return vehicleProfileReady(profile_); }
  const VehicleProfile& profile() const { return profile_; }
 private:
  VehicleProfile profile_;
  bool hasCounter_ = false;
  uint32_t counter_ = 0, counterAt_ = 0;
};
class RpmState {
 public:
  explicit RpmState(RpmMapping mapping) : mapping_(mapping) {}
  explicit RpmState(VehicleProfile profile) : vehicle_(true), vehicleDecoder_(profile) {}
  DecodeResult ingest(const CanFrame& frame);
  // Call each application tick. Once stale, a value cannot revive after a full
  // 32-bit timer lap; only a new validated frame clears this expiration latch.
  void expire(uint32_t now, uint32_t rpmTimeout, uint32_t busTimeout) {
    if (hasFrame_ && uint32_t(now - frameAt_) >= busTimeout) busExpired_ = true;
    if (hasValue_ && !expired_ && uint32_t(now - validAt_) >= rpmTimeout) {
      expired_ = true; vehicleDecoder_.resetContinuity();
    }
  }
  bool fresh(uint32_t now, uint32_t timeout) const { return hasValue_ && !invalidated_ && !expired_ && uint32_t(now - validAt_) < timeout; }
  bool busFresh(uint32_t now, uint32_t timeout) const { return hasFrame_ && !busExpired_ && uint32_t(now - frameAt_) < timeout; }
  bool hasValue() const { return hasValue_; }
  bool hasFrame() const { return hasFrame_; }
  bool mapped() const { return vehicle_ ? vehicleDecoder_.ready() : mapping_.enabled; }
  bool signalInvalid() const { return invalidated_; }
  uint32_t timeoutMs(uint32_t fallback) const { return vehicle_ ? vehicleDecoder_.profile().timeoutMs : fallback; }
  uint16_t rpm() const { return rpm_; }
  uint32_t lastId() const { return lastId_; }
  uint32_t accepted() const { return accepted_; }
  uint32_t rejected() const { return rejected_; }
  DecodeResult lastResult() const { return lastResult_; }
 private:
  RpmMapping mapping_{};
  bool vehicle_ = false, invalidated_ = false, expired_ = false, busExpired_ = false;
  VehicleDecoder vehicleDecoder_{VehicleProfile{}};
  bool hasValue_ = false, hasFrame_ = false;
  uint16_t rpm_ = 0;
  uint32_t validAt_ = 0, frameAt_ = 0, lastId_ = 0, accepted_ = 0, rejected_ = 0;
  DecodeResult lastResult_ = DecodeResult::Disabled;
};
