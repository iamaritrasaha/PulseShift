// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "rpm_decoder.h"
DecodeResult decodeRpm(const CanFrame& f, const RpmMapping& m, uint16_t& rpm) {
  if (!validEnvelope(f)) return DecodeResult::Malformed;
  if (f.remote) return DecodeResult::Remote;
  if (!m.enabled) return DecodeResult::Disabled;
  if (m.dlc > 8 || m.dlc < 2 || m.offset > m.dlc - 2 || m.denominator == 0 ||
      m.numerator == 0 || m.maxRpm == 0 || m.id > (m.extended ? 0x1FFFFFFFUL : 0x7FFUL)) return DecodeResult::BadMapping;
  if (f.id != m.id || f.extended != m.extended) return DecodeResult::WrongId;
  if (f.dlc != m.dlc) return DecodeResult::WrongDlc;
  const uint16_t a = f.data[m.offset], b = f.data[m.offset + 1];
  const uint16_t raw = m.littleEndian ? (a | (b << 8)) : ((a << 8) | b);
  const uint64_t scaled = uint64_t(raw) * m.numerator;
  // Reject above the physical range BEFORE integer truncation; division floors.
  if (scaled > uint64_t(m.maxRpm) * m.denominator) return DecodeResult::OutOfRange;
  rpm = uint16_t(scaled / m.denominator);
  return DecodeResult::Valid;
}
DecodeResult RpmState::ingest(const CanFrame& f) {
  if (validEnvelope(f)) { hasFrame_ = true; frameAt_ = f.timestampMs; lastId_ = f.id; }
  uint16_t decoded = 0;
  lastResult_ = decodeRpm(f, mapping_, decoded);
  if (lastResult_ == DecodeResult::Valid) {
    rpm_ = decoded; validAt_ = f.timestampMs; hasValue_ = true; ++accepted_;
  } else { ++rejected_; }
  return lastResult_;
}
const char* decodeName(DecodeResult r) {
  switch (r) {
    case DecodeResult::Valid: return "VALID";
    case DecodeResult::Disabled: return "UNMAPPED";
    case DecodeResult::Malformed: return "MALFORMED";
    case DecodeResult::Remote: return "RTR";
    case DecodeResult::WrongId: return "WRONG_ID";
    case DecodeResult::WrongDlc: return "WRONG_DLC";
    case DecodeResult::BadMapping: return "BAD_MAPPING";
    case DecodeResult::OutOfRange: return "OUT_OF_RANGE";
  }
  return "UNKNOWN";
}
