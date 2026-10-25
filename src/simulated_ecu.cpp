#include "simulated_ecu.h"
RpmMapping demonstrationMapping() {
  RpmMapping m; m.enabled = true; m.id = 0x555; return m;
}
CanFrame demonstrationFrame(uint32_t now, uint32_t sequence) {
  CanFrame f; f.id = 0x555; f.dlc = 8; f.timestampMs = now;
  const uint32_t step = sequence % 131;
  const uint16_t raw = (step <= 65 ? step : 130 - step) * 100;
  f.data[0] = uint8_t(raw); f.data[1] = uint8_t(raw >> 8);
  for (unsigned i = 0; i < 4; ++i) f.data[i+2] = uint8_t(sequence >> (8*i));
  f.data[6] = 0xA5; f.data[7] = 0x5A;
  return f;
}
bool ReplayTransport::receive(uint32_t now, CanFrame& f) {
  if (uint32_t(now-last_) < 100) return false;
  last_ = now;
  // Every 20s: malformed/out-of-range traffic, then silence, then recovery.
  const uint32_t phase = now % 20000;
  if (phase >= 14000 && phase < 17000) return false;
  f = demonstrationFrame(now, sequence_++);
  if (phase >= 12000 && phase < 13000) f.dlc = 1;
  if (phase >= 13000 && phase < 14000) { f.data[0] = 0xFF; f.data[1] = 0xFF; }
  return true;
}
