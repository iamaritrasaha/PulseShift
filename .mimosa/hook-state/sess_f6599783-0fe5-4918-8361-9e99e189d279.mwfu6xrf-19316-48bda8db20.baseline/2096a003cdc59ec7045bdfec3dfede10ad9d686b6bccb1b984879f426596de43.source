// Regression: candidate RPM output must respect the actual firmware diagnostic setting.
// Test this as a separate translation unit, just like Arduino builds it.
#include "rpm_candidate.h"

int main() {
  RpmCandidateMonitor monitor;
  monitor.printStreamLine(100); // Waiting state must print when enabled.

  CanFrame frame{};
  frame.id = 0x301;
  frame.dlc = 8;
  frame.data[0] = 44;
  if (!monitor.ingest(frame, 200)) return 1;
  monitor.printStreamLine(210);
  monitor.printStatus(210, true, 500000, true);
  return 0;
}
