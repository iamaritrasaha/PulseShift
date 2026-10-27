// Copyright (c) 2026 Aritra Saha. All rights reserved.
#include "vehicle_profile.h"
#include <limits.h>
bool validSignal(const CanSignal& s, uint8_t dlc) {
  return dlc <= 8 && s.width >= 1 && s.width <= 32 &&
    unsigned(s.startBit) + s.width <= unsigned(dlc) * 8 &&
    (s.order == SignalOrder::LittleEndianLsb0 || s.order == SignalOrder::BigEndianMsb0);
}
bool extractSignal(const CanFrame& f, const CanSignal& s, uint32_t& raw) {
  if (!validEnvelope(f) || f.remote || !validSignal(s, f.dlc)) return false;
  uint32_t value = 0;
  for (unsigned i = 0; i < s.width; ++i) {
    const unsigned bit = s.startBit + i;
    if (s.order == SignalOrder::LittleEndianLsb0)
      value |= uint32_t((f.data[bit/8] >> (bit%8)) & 1U) << i;
    else value = (value << 1) | ((f.data[bit/8] >> (7-bit%8)) & 1U);
  }
  raw = value;
  return true;
}
namespace {
bool validMatch(const MatchRule& r, uint8_t dlc) {
  return !r.enabled || (validSignal(r.signal, dlc) &&
    (r.signal.width == 32 || r.expected < (uint32_t(1) << r.signal.width)));
}
}
bool validProfileDefinition(const VehicleProfile& p) {
  const bool rate = p.bitrate == 125000 || p.bitrate == 250000 ||
    p.bitrate == 500000 || p.bitrate == 1000000;
  if (!rate || !p.identifier || !*p.identifier || !p.description || !*p.description ||
      p.arbitrationId > (p.extended ? 0x1FFFFFFFUL : 0x7FFUL) ||
      !validSignal(p.rpm, p.requiredDlc) || p.scaleNumerator == 0 ||
      p.scaleNumerator > INT32_MAX || p.scaleDenominator == 0 ||
      p.maximumRpm == 0 || p.minimumRpm > p.maximumRpm ||
      p.timeoutMs == 0 || p.timeoutMs > 0x7FFFFFFFUL ||
      !validMatch(p.validity, p.requiredDlc) || !validMatch(p.multiplexer, p.requiredDlc))
    return false;
  const auto& c = p.checksum;
  if (c.kind != ChecksumKind::None) {
    if ((c.kind != ChecksumKind::Sum8 && c.kind != ChecksumKind::Xor8) ||
        c.byteCount == 0 || unsigned(c.firstByte) + c.byteCount > p.requiredDlc ||
        c.checksumByte >= p.requiredDlc ||
        (c.checksumByte >= c.firstByte && c.checksumByte < unsigned(c.firstByte) + c.byteCount))
      return false;
  }
  if (p.counter.enabled && (!validSignal(p.counter.signal, p.requiredDlc) ||
      p.counter.signal.width > 16 || p.counter.modulus < 2 ||
      p.counter.modulus > (uint32_t(1) << p.counter.signal.width))) return false;
  const auto& l = p.lights;
  return l.start < l.shift && l.shift <= l.maximum && l.maximum <= p.maximumRpm &&
    l.hysteresis < l.shift - l.start;
}
bool vehicleProfileReady(const VehicleProfile& p) {
  const auto& e = p.evidence;
  return p.status == ProfileStatus::VERIFIED && e.reference && *e.reference &&
    e.repeatedCaptures && e.independentRpm && e.electricalQualification &&
    e.shiftCalibrationVerified && validProfileDefinition(p);
}
const char* profileStatusName(ProfileStatus s) {
  switch (s) {
    case ProfileStatus::UNVERIFIED: return "UNVERIFIED";
    case ProfileStatus::CAPTURE_ONLY: return "CAPTURE_ONLY";
    case ProfileStatus::VERIFIED: return "VERIFIED";
  }
  return "INVALID";
}
const VehicleProfile& selectedVehicleProfile() {
  static const VehicleProfile ns125 = [] {
    VehicleProfile p;
    p.identifier = "ns125-india-2025-abs";
    p.description = "2025 Indian Bajaj Pulsar NS125 ABS; mapping and electrical qualification pending";
    // Owner reports RPM at the interface. No passive capture/pinout/rate is supplied.
    // Leave protocol fields empty rather than populated with demonstration guesses.
    return p;
  }();
  return ns125;
}
