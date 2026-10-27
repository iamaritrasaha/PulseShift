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
DecodeResult VehicleDecoder::decode(const CanFrame& f, uint16_t& rpm) {
  if (!validEnvelope(f)) return DecodeResult::Malformed;
  if (f.remote) return DecodeResult::Remote;
  if (!ready()) return DecodeResult::ProfileUnconfigured;
  const auto& p = profile_;
  if (f.id != p.arbitrationId || f.extended != p.extended) return DecodeResult::WrongId;
  if (f.dlc != p.requiredDlc) return DecodeResult::WrongDlc;
  uint32_t raw = 0;
  if (p.multiplexer.enabled) {
    extractSignal(f, p.multiplexer.signal, raw);
    if (raw != p.multiplexer.expected) return DecodeResult::WrongMux;
  }
  if (p.validity.enabled) {
    extractSignal(f, p.validity.signal, raw);
    if (raw != p.validity.expected) return DecodeResult::InvalidValue;
  }
  const auto& c = p.checksum;
  if (c.kind != ChecksumKind::None) {
    uint8_t sum = c.seed;
    for (unsigned i = c.firstByte; i < unsigned(c.firstByte) + c.byteCount; ++i)
      sum = c.kind == ChecksumKind::Sum8 ? uint8_t(sum + f.data[i]) : uint8_t(sum ^ f.data[i]);
    if (sum != f.data[c.checksumByte]) return DecodeResult::BadChecksum;
  }
  if (p.additionalValidation && !p.additionalValidation(f)) return DecodeResult::RuleRejected;
  extractSignal(f, p.rpm, raw);
  // Definition limits guarantee the signed 64-bit operation cannot overflow.
  const int64_t scaled = int64_t(raw) * p.scaleNumerator + p.offsetNumerator;
  if (scaled < int64_t(p.minimumRpm) * p.scaleDenominator ||
      scaled > int64_t(p.maximumRpm) * p.scaleDenominator) return DecodeResult::OutOfRange;
  if (p.counter.enabled) {
    uint32_t next = 0;
    extractSignal(f, p.counter.signal, next);
    if (next >= p.counter.modulus) return DecodeResult::BadCounter;
    const bool continuous = hasCounter_ && uint32_t(f.timestampMs - counterAt_) < p.timeoutMs;
    const bool correct = !continuous || next == (counter_ + 1) % p.counter.modulus;
    // A rejected discontinuity seeds a baseline; only a subsequent increment is
    // accepted. After a stale gap, continuity cannot be established.
    hasCounter_ = true; counter_ = next; counterAt_ = f.timestampMs;
    if (!correct) return DecodeResult::BadCounter;
  }
  rpm = uint16_t(scaled / p.scaleDenominator);
  return DecodeResult::Valid;
}
DecodeResult RpmState::ingest(const CanFrame& f) {
  if (validEnvelope(f)) { hasFrame_ = true; busExpired_ = false; frameAt_ = f.timestampMs; lastId_ = f.id; }
  uint16_t decoded = 0;
  lastResult_ = vehicle_ ? vehicleDecoder_.decode(f, decoded) : decodeRpm(f, mapping_, decoded);
  if (lastResult_ == DecodeResult::Valid) {
    rpm_ = decoded; validAt_ = f.timestampMs; hasValue_ = true; invalidated_ = false; expired_ = false; ++accepted_;
  } else {
    ++rejected_;
    if (vehicle_ && vehicleDecoder_.ready() && f.id == vehicleDecoder_.profile().arbitrationId &&
        f.extended == vehicleDecoder_.profile().extended &&
        lastResult_ != DecodeResult::WrongMux) invalidated_ = true;
  }
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
    case DecodeResult::ProfileUnconfigured: return "PROFILE_UNCONFIGURED";
    case DecodeResult::InvalidValue: return "INVALID_VALUE";
    case DecodeResult::WrongMux: return "WRONG_MUX";
    case DecodeResult::BadChecksum: return "BAD_CHECKSUM";
    case DecodeResult::BadCounter: return "BAD_COUNTER";
    case DecodeResult::RuleRejected: return "RULE_REJECTED";
  }
  return "UNKNOWN";
}
