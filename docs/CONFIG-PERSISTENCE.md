# Configuration persistence and physical LED count

Firmware 2.4.0 keeps the existing BLE configuration packet version, snapshot schema, six zone colors and six RPM thresholds. Configuration protocol minor 4 adds the following extensions.

| Field | Meaning |
| --- | --- |
| Capability bit 21 | Variable strip configuration. Snapshot TLV 0x1F holds five bytes: extension version 1, configured count, running count, minimum, maximum. |
| Command 0x1E | Set physical count using two bytes: extension version 1 and count. Supports the existing staged flag. Requires an owner session. |
| Capability bit 22 | Verified save. SAVE_CONFIG accepts an optional expected revision as a little-endian u32. The reply for this form includes one save-status byte. The legacy zero-length request remains accepted. |
| Snapshot TLV 0x20 | Two bytes: extension version 1 and whether the active configuration matches a verified flash record. |
| Diagnostics TLV 0x4B | Eight bytes: extension version 1, boot load status, last save status, active flash match, stored revision as a little-endian u32. |

Boot load statuses are unavailable (0), no record (1), validated v4 (2), migrated legacy (3), recovered legacy after v4 validation failed (4), invalid data with safe defaults (5), and a valid v4 record with an invalid strip count defaulted to six (6). Save statuses are no attempt (0), readback verified (1), NVS open failed (2), write or commit failed (3), readback failed (4), and invalid configuration (5). A save cannot proceed while a transaction is staged.

The application loads and validates configuration before initializing LED output. An explicit save commits one CRC-protected record to 'ps_cfg/blob4', closes the NVS handle, reopens it read-only and compares the stored bytes. The boot path validates the header, length, CRC and decoded setting bounds. The legacy 'ps_cfg/blob' remains readable and is preserved across saves. Schema 3 retains its calibration binding. Schema 2 calibration claims lack the binding fields and require verification again. The startup ID and packed cycle duration retain the same encoding, with bit 15 indicating continuous mode.

The count used by RMT is latched at boot. Setting or reloading a count during a running session changes the configuration and leaves the RMT channel and its in-flight symbols alone. Reboot after a verified save to apply the new count. The physical pixel with logical index i uses zone floor(i × 6 / N), where N is the running count. Direction reverses the complete rendered frame. This preserves six-stage behavior at six LEDs. A short strip can represent only the zones mapped to its available pixels.

The implementation bounds counts to 1 through 64. For 64 pixels the encoder supplies 1,537 symbols, including a 300 µs reset interval. The encoded frame is at most 2.451 ms at the configured RMT clock. This is an encoded timing bound. The classic ESP32 streams long frames through its non-DMA RMT memory and can be sensitive to interrupt latency. Validate the target strip on GPIO27 under sustained BLE traffic before bench sign-off. Use a suitably sized 5 V strip supply with a common ground and budget up to 60 mA per LED at full white.

## Rollback

An older application reads the preserved legacy configuration from 'ps_cfg/blob' and uses its original strip length. Settings written by 2.4.0 remain in 'ps_cfg/blob4' and load when returning to 2.4.0. Saves made by a rolled-back firmware update the legacy record. Keep a device backup and record settings before any downgrade. A legacy revision may have no saved record on a new installation. Leave the partition table, owner namespace and LittleFS intact when updating or rolling back.
