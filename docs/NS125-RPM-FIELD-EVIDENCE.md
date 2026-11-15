# NS125 CAN RPM: field observations and verification status

This is an evidence ledger for the *existing* firmware defaults. It does **not**
reclassify observational field correlation as completed bound calibration.

## Established on the motorcycle (November 2026)

- Real passive CAN reception was achieved at **500 kbps** with an SN65HVD230
  and ESP32 on the Bajaj Pulsar NS125 2025 ABS.
- An earlier three-stage recording (key-on/engine-off, idle, gentle RPM sweeps)
  produced **22,542 frames** across **18 CAN IDs** without reported record CRC
  corruption. The candidate **standard CAN ID 0x301, data byte 0** tracked
  engine state: **raw 0** with the engine off, roughly **35-40** at idle,
  and varying upward with revs.
- Using the existing multiplier **40.0**, raw **37** corresponds to **1480 RPM**.
  In a separate real-bike observation, the companion reading agreed with the
  motorcycle dashboard near **3000 RPM**.
- There is an unresolved idle disagreement: the dashboard appeared to indicate
  approximately **1000 RPM** while the companion reported **1300-1500 RPM**.
  Dashboard granularity and reference accuracy were not established.

## Additional preserved capture (November 15, 2026)

A later **23,370-frame**, **18-ID** recording contained only **Stage 1
(key-on/engine-off)**, with **0 reported CRC errors**. All **2,940** ID `0x301`
frames had byte 0 equal to zero. Its separate saved copy was independently
verified on the user's PC. **Do not commit the private raw captures to GitHub.**

## Engineering decision

- Keep the current default **ID `0x301`, standard 11-bit, byte index 0,
  multiplier `40.0`, zero offset**. They have on-bike supporting evidence;
  arbitrarily replacing them would be less justified.
- Label this **FIELD-CORRELATED**, not formally **CALIBRATION VERIFIED**.
- Do not fabricate the firmware's calibration fingerprint, three independent
  reference points, residual check, or `calibrationVerified` NVS bit. The
  verified `RPM Bar` effect remains gated; effect 5 is an explicitly
  *provisional* preview, **not a validated shift signal**.
- No repeated idle/rev capture is requested in this software maintenance task.
  Future formal acceptance, if required to enable the verified bar mode, must
  use the existing supported calibration workflow and actual measurements.

## Software regression evidence

Host tests include engine-off raw=0, idle raw=37 -> 1480 RPM, nonzero
raw=75 -> 3000 RPM, source/DLC rejection, and stale signal cutoff.
These are decoder arithmetic and safety tests; **not** independent calibration.
