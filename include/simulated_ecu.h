#pragma once
#include "can_frame.h"
#include "rpm_decoder.h"
// Demonstration protocol only: ID 555, exact DLC 8, RPM LE16, sequence LE32,
// bytes 6..7 = A5 5A. Neither the ID nor the encoding describes the NS125.
RpmMapping demonstrationMapping();
CanFrame demonstrationFrame(uint32_t now, uint32_t sequence);
class ReplayTransport {
 public:
  bool receive(uint32_t now, CanFrame& frame);
 private:
  uint32_t last_ = 0, sequence_ = 0;
};
