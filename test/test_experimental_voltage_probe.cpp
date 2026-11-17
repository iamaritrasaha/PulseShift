// Read-only experimental voltage mapping; host tests do not access bike/CAN.
#include "experimental_voltage_probe.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  auto& probe = ExperimentalVoltageProbe::instance();

  // Supported, but no CAN 0x303 frame received yet.
  assert(probe.bleReservedWord(10) == 0x5A00u);

  CanFrame rpm{};
  rpm.id = 0x301;
  rpm.dlc = 8;
  rpm.data[1] = 122;
  probe.ingest(rpm, 100);
  assert(probe.bleReservedWord(100) == 0x5A00u);

  CanFrame voltage{};
  voltage.id = 0x303;
  voltage.dlc = 8;
  voltage.payloadSize = 8;
  voltage.data[1] = 0x7Au;
  probe.ingest(voltage, 100);
  assert(probe.bleReservedWord(100) == 0xA57Au);
  assert(probe.bleReservedWord(1100) == 0xA57Au);
  assert(probe.bleReservedWord(1101) == 0x5A00u);

  // Spoofed invalid envelopes / remote / extended must not refresh or overwrite.
  voltage.remote = true;
  voltage.data[1] = 200;
  probe.ingest(voltage, 2000);
  assert(probe.bleReservedWord(2000) == 0x5A00u);
  voltage.remote = false;
  voltage.extended = true;
  probe.ingest(voltage, 2100);
  assert(probe.bleReservedWord(2100) == 0x5A00u);
  voltage.extended = false;
  voltage.dlc = 1;
  probe.ingest(voltage, 2200);
  assert(probe.bleReservedWord(2200) == 0x5A00u);

  voltage.dlc = 8;
  voltage.data[1] = 135;
  probe.ingest(voltage, 3000);
  assert(probe.bleReservedWord(3000) == 0xA587u);

  // Stale on wrap-around arithmetic: no signed overflow.
  probe.ingest(voltage, 0xFFFFFF00u);
  assert(probe.isFresh(0x00000050u));
  assert(!probe.isFresh(0x00000400u));
  std::puts("experimental voltage CAN probe PASS");
  return 0;
}
