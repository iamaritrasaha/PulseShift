// Copyright (c) 2026 Aritra Saha. All rights reserved.
#pragma once
#include "can_frame.h"
enum class DecodeResult { Valid, Disabled, Malformed, Remote, WrongId, WrongDlc, BadMapping, OutOfRange };
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
class RpmState {
 public:
  explicit RpmState(RpmMapping mapping) : mapping_(mapping) {}
  DecodeResult ingest(const CanFrame& frame);
  bool fresh(uint32_t now, uint32_t timeout) const { return hasValue_ && uint32_t(now - validAt_) < timeout; }
  bool busFresh(uint32_t now, uint32_t timeout) const { return hasFrame_ && uint32_t(now - frameAt_) < timeout; }
  bool hasValue() const { return hasValue_; }
  bool hasFrame() const { return hasFrame_; }
  bool mapped() const { return mapping_.enabled; }
  uint16_t rpm() const { return rpm_; }
  uint32_t lastId() const { return lastId_; }
  uint32_t accepted() const { return accepted_; }
  uint32_t rejected() const { return rejected_; }
  DecodeResult lastResult() const { return lastResult_; }
 private:
  RpmMapping mapping_;
  bool hasValue_ = false, hasFrame_ = false;
  uint16_t rpm_ = 0;
  uint32_t validAt_ = 0, frameAt_ = 0, lastId_ = 0, accepted_ = 0, rejected_ = 0;
  DecodeResult lastResult_ = DecodeResult::Disabled;
};
