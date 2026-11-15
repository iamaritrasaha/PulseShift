# PulseShift v2.3.0 OTA Bench-Test Plan and Hardware Verification

This document defines the comprehensive hardware bench-test procedure for validating over-the-air (OTA) firmware updates, cryptographic authenticity, automatic bootloader rollback, and emergency recovery on physical ESP32 DevKit V1 hardware.

> **MANDATORY RELEASE GATE:** OTA functionality must never be marked production-ready based on simulation or unit tests alone. It must be demonstrated on real hardware through all failure injection scenarios described in this document.

---

## 1. Bench Hardware Setup and Pre-Flight Checks

Perform all OTA tests on a dedicated test bench before flashing or deploying on a vehicle.

### 1.1 Physical Wiring
- **ESP32 DevKit V1:** Powered via stable external 5V bench power supply (monitored with digital multimeter: 4.85V to 5.15V). Do not run high-current flashing tests from unpowered USB hubs.
- **SN65HVD230 CAN Transceiver:**
  - `3V3` and `GND` connected to ESP32 3.3V rail and system ground.
  - `CRX` connected to GPIO 16 (TWAI RX).
  - `CTX` connected to 3.3V rail through a pull-up or clamped to logic HIGH recessive state (GPIO 17 is completely isolated / air-gapped via firmware `gpio_reset_pin`).
  - `CANH` / `CANL`: Open circuit on bench (or terminated with 60 ohm net for simulated traffic tests).
- **WS2812B 6-LED Array:**
  - `5V` and `GND` connected to external 5V power supply.
  - `DIN` connected to GPIO 27 through a 330 ohm series damping resistor.
- **USB-UART Interface:** Connected to PC for continuous diagnostic serial capture at 115200 baud.

### 1.2 Bootloader and Partition Verification
The production firmware relies on the standard ESP32 dual-OTA partition scheme (`default.csv`):
- `bootloader.bin`: `0x1000` (ESP-IDF 4.4 / Arduino core with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1`)
- `partitions.bin`: `0x8000` (NVS at `0x9000`, otadata at `0xe000`, `app0` at `0x10000`, `app1` at `0x150000`, `spiffs` at `0x290000`)
- `boot_app0.bin`: `0xe000` (Initial otadata pointing to active `app0`)
- `app0` slot: 1,310,720 bytes (1.25 MB)
- `app1` slot: 1,310,720 bytes (1.25 MB)

Run `esptool.py` to confirm the partition table if verifying an unknown board:
```bash
esptool.py --chip esp32 --port <PORT> read_flash 0x8000 0xC00 partition_dump.bin
gen_esp32part.py partition_dump.bin
```

---

## 2. Firmware Build and Cryptographic Signing

The PulseShift OTA pipeline enforces cryptographic image authenticity using NIST P-256 ECDSA signatures with SHA-256 digests.

### 2.1 Private Signing Key Security
- Private keys must **never** be committed to Git.
- The project private key is stored in `tools/keys/pulseshift_ota_key.pem` (protected by `.gitignore`).
- The uncompressed 65-byte SEC1 public key is embedded in `PulseShift/src/ota_public_key.h`.
- To generate a new deployment keypair (if rotating keys):
  ```bash
  uv run --with cryptography python tools/sign_firmware.py generate-key \
    --key tools/keys/pulseshift_ota_key.pem \
    --header PulseShift/src/ota_public_key.h
  ```

### 2.2 Compilation and Signing Workflow
1. Compile the firmware binary:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 D:\Development\PulseShift-Firmware\PulseShift
   ```
2. Sign the binary and attach the 76-byte `PSOT` authentication trailer:
   ```bash
   uv run --with cryptography python tools/sign_firmware.py sign \
     --input D:\Development\PulseShift-Firmware\build\esp32.esp32.esp32doit-devkit-v1\PulseShift.ino.bin \
     --key tools/keys/pulseshift_ota_key.pem \
     --output PulseShift_signed.bin
   ```
3. Verify the signature locally before transmission:
   ```bash
   uv run --with cryptography python tools/sign_firmware.py verify \
     --input PulseShift_signed.bin \
     --public-key tools/keys/pulseshift_ota_key.pem
   # Note: --key is also accepted as a convenience alias.
   # You may also verify directly against the uncompressed SEC1 hex string from ota_public_key.h:
   uv run --with cryptography python tools/sign_firmware.py verify \
     --input PulseShift_signed.bin \
     --public-key 040072cffa71b451afe19b5472fe56220653ceb6ae1b534459f7be14ddc6939336cb0ea85068b48c51fda40f3af4725473085b51f1c4b1660e017fd1073ed1e13a
   ```

---

## 3. Bench Verification Scenarios

### Scenario 1: Complete Valid Image Transfer and Boot Confirmation
- **Objective:** Verify normal, end-to-end OTA update over BLE with MTU negotiation, signature validation, and clean boot.
- **Steps:**
  1. Boot ESP32 into `app0` (firmware v2.3.0). Confirm serial shows active BLE advertising.
  2. Open Android companion app and connect to PulseShift.
  3. Verify Android negotiates MTU 512 (`_currentMtu` updates to 512, chunk size adapts to 215-230 bytes).
  4. Select `PulseShift_signed.bin` and initiate OTA update.
  5. Observe transfer progress: ~3,200 chunks transmitted in approximately 35-45 seconds (versus ~68,000 packets / 15 minutes without extended MTU).
  6. Confirm ESP32 logs CRC32 match, validates ECDSA P-256 signature, validates `esp_image_verify`, and calls `esp_ota_set_boot_partition(app1)`.
  7. ESP32 reboots into `app1`. Boot status shows `ESP_OTA_IMG_PENDING_VERIFY`.
  8. Firmware self-tests execute successfully (LittleFS mount, NVS config, CAN listen-only, BLE advertising).
  9. Android reconnects, verifies running slot is `app1` and version matches target.
  10. Android sends `OTA_CONFIRM` (or bench engineer types `OTA CONFIRM` in serial console). ESP32 calls `esp_ota_mark_app_valid_cancel_rollback()`, marks the partition valid, and cancels rollback.
- **Pass Criteria:** Firmware is permanently confirmed in `app1`; subsequent manual power-cycle boots into `app1`.

---

### Scenario 1B: Rollback on Missing Confirmation or Timeout
- **Objective:** Verify that if the companion app disconnects, crashes, or fails to confirm the update within 120 seconds, the firmware automatically rolls back to the prior known-good slot.
- **Steps:**
  1. Complete steps 1-8 of Scenario 1 (flashing `app1` and rebooting into `ESP_OTA_IMG_PENDING_VERIFY`).
  2. Disconnect Android Bluetooth (or keep the companion app closed) and do not issue `OTA CONFIRM`.
  3. Observe serial console: at exactly 120 seconds uptime, the operational grace period expires.
  4. ESP32 logs `[OTA TIMEOUT] Operational confirmation not received within 120s! Rolling back to prior slot...` and invokes `esp_ota_mark_app_invalid_rollback_and_reboot()`.
  5. The bootloader marks `app1` invalid and boots back into known-good `app0`.
- **Pass Criteria:** Board recovers automatically to `app0` without operator intervention. Subsequent boots remain on `app0`.

---

### Scenario 2: Safety Interlock Validation (Engine Running or Active CAN Traffic)
- **Objective:** Verify that OTA updates are strictly blocked if the vehicle is operating.
- **Test 2A - Engine Running:**
  1. Inject synthetic RPM > 0 via serial test command or test stub.
  2. Attempt to initiate OTA from Android app.
  3. **Pass Criteria:** ESP32 immediately rejects `OTA_BEGIN` with error status `BLE_CFG_STATUS_INVALID_STATE`. No flash writes occur.
- **Test 2B - Active CAN Traffic:**
  1. Inject CAN traffic into the transceiver RX line so that `transport.hasTraffic(now)` is true (or traffic received within last 3,000 ms).
  2. Attempt to initiate OTA update from Android app.
  3. **Pass Criteria:** ESP32 immediately rejects `OTA_BEGIN` with `BLE_CFG_STATUS_INVALID_STATE`.

---

### Scenario 3: Unauthenticated / Invalid Signature Rejection
- **Objective:** Verify cryptographic rejection of unsigned or improperly signed images.
- **Test 3A - Unsigned Binary:**
  1. Attempt to upload raw `PulseShift.ino.bin` without `PSOT` trailer.
  2. **Pass Criteria:** Android companion app rejects the file immediately. If sent directly via protocol, ESP32 rejects at `endOta()` with `BLE_CFG_STATUS_OTA_AUTH_FAIL`. Inactive partition is erased and device does not reboot.
- **Test 3B - Rogue / Invalid Key Signature:**
  1. Generate an unauthorized temporary ECDSA key and sign the firmware.
  2. Transmit the image via BLE OTA.
  3. **Pass Criteria:** ESP32 validates CRC32 but ECDSA signature verification against `PULSESHIFT_OTA_PUBLIC_KEY` fails. ESP32 returns `BLE_CFG_STATUS_OTA_AUTH_FAIL`. Boot partition remains unchanged.

---

### Scenario 4: Corrupted Data and Truncated Transfer Rejection
- **Objective:** Verify image integrity protection against communication bit-flips or premature disconnect.
- **Test 4A - Corrupted Byte in Transit:**
  1. Modify a single byte inside an OTA data chunk.
  2. **Pass Criteria:** Running CRC32 check fails on ESP32 at `endOta()`. ESP32 returns `BLE_CFG_STATUS_OTA_CHECKSUM_ERROR`. Boot partition remains unchanged.
- **Test 4B - Truncated Transfer:**
  1. Send fewer bytes than declared in `OTA_BEGIN`, then send `OTA_END`.
  2. **Pass Criteria:** ESP32 detects byte count mismatch (`bytesWritten_ != imageSize_`), rejects with `BLE_CFG_STATUS_INVALID_PARAM`.
- **Test 4C - Disconnect Recovery:**
  1. Abruptly disable phone Bluetooth at 50% transfer.
  2. Wait 30 seconds for BLE connection timeout.
  3. **Pass Criteria:** ESP32 aborts OTA session, cleans up partition handle, resumes normal operation, and retains active running partition.

---

### Scenario 5: Intentional First-Boot Failure and Automatic Rollback
- **Objective:** Verify hardware rollback when a newly flashed image fails its startup self-test.
- **Steps:**
  1. Build firmware with the boot-failure fault injection flag:
     ```bash
     arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 \
       --build-property "compiler.cpp.extra_flags=-DPULSESHIFT_TEST_FORCE_BOOT_FAIL=1" \
       PulseShift
     ```
  2. Sign the binary with `tools/sign_firmware.py`.
  3. Upload the binary via Android companion app OTA.
  4. Device writes image to `app1`, sets boot partition, and reboots.
  5. Upon boot in `app1`, firmware detects `PULSESHIFT_TEST_FORCE_BOOT_FAIL=1` in `setup()` and triggers:
     ```cpp
     esp_ota_mark_app_invalid_rollback_and_reboot();
     ```
  6. ESP32 hardware watchdog / bootloader intercepts invalid state and automatically rolls back boot partition to `app0`.
  7. ESP32 boots back into known-good `app0`.
  8. Android companion app reconnects, inspects `otaActiveSlot`, observes that running slot remains `app0` instead of target `app1`, and explicitly reports rollback failure.
- **Pass Criteria:** ESP32 automatically recovers back to `app0`; Android app never claims false success.

---

### Scenario 6: Power Interruption During Write
- **Objective:** Verify flash resilience when power is cut mid-transfer.
- **Steps:**
  1. Begin valid OTA transfer.
  2. At approximately 40% progress, physically disconnect the 5V power supply to the ESP32.
  3. Wait 10 seconds. Reapply 5V power.
  4. Inspect serial monitor output.
- **Pass Criteria:** ESP32 boots cleanly from the active slot (`app0`). Stored NVS configuration and LittleFS CAN captures are 100% intact. Android app reconnects normally.

---

## 4. Universal USB Fallback Recovery Procedure

> **CRITICAL ARCHITECTURAL DISTINCTION:** Writing only the application binary at `0x10000` is **NOT** a universal recovery procedure. If a power cut or partition corruption damages the bootloader, partition table, or `otadata` slots, writing only `0x10000` will result in an unbootable boot loop.

Universal recovery requires flashing all 4 foundational flash components or executing a full flash erase.

### 4.1 Full Four-Artifact Recovery Flash
Locate the compiled binaries from the Arduino IDE build cache or ESP-IDF build directory and flash using `esptool.py`:
```bash
esptool.py --chip esp32 --port <PORT> --baud 921600 write_flash \
  0x1000  bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 PulseShift.ino.bin
```

### 4.2 Factory Erase and Clean Reflash (When Flash Layout is Corrupted)
If partition table corruption or filesystem damage occurs:
1. Completely erase flash:
   ```bash
   esptool.py --chip esp32 --port <PORT> erase_flash
   ```
2. Reflash the complete build using Arduino IDE (Upload) or via the four-artifact `write_flash` command above.
3. Open Serial Monitor at 115200 baud. Type `STATUS` to verify LittleFS initialization and clean NVS profile generation.

---

## 4.2 Arduino-ESP32 deferred OTA confirmation check

Arduino-ESP32 core 2.0.x invokes a weak `verifyRollbackLater()` hook before
the sketch's `setup()`. Without an override, the core can immediately mark a
new OTA image VALID, bypassing the product's 120-second owner-confirmation gate.
PulseShift now provides a strong C-linkage override returning `true`, leaving
the image in `ESP_OTA_IMG_PENDING_VERIFY` until the boot self-test and an
explicit authenticated `OTA_CONFIRM` (or USB bench `OTA CONFIRM`).

**On a real ESP32, verify the bootloader and both app slots:**

1. Keep the board on stable USB power on the bench, *not on the motorcycle*.
2. Use Serial Monitor at 115200 baud. Send `OTA STATUS` to inspect the active
   app slot, actual OTA image state, and whether a rollback candidate exists.
3. After a valid signed OTA but **before confirmation**, expect
   `PENDING_VERIFY` and a valid rollback candidate.
4. After successful Android confirmation, expect `VALID`. Reboot and check
   that the same slot remains active.
5. In a separate supervised rollback test, leave an update unconfirmed; after
   the 120-second firmware timeout the prior image should boot. If the device
   stays on the new image, **do not deploy OTA or claim rollback protection**.
6. A pending image will normally roll back if power is cut or the device
   resets before confirmation. Do not use a power interruption as the first
   test when valuable CAN recordings have not been exported.

The Android companion queries the extended OTA_STATUS response (8 payload
bytes, including image state and rollback candidate), completes owner
reauthentication on the **new** BLE connection, and only then issues OTA_CONFIRM.
A confirmed OTA requires both a successful acknowledgment and an appropriate
bootloader image state. Inconsistent state fails closed.

---

## 5. Bench Test Verification Sign-Off Matrix

| Test ID | Description | Expected Result | Status |
|---|---|---|---|
| TC-01 | Full OTA transfer over negotiated MTU 512 | Transfers in <45s, verifies signature, boots into new slot | **PENDING PHYSICAL BENCH TEST** |
| TC-02 | Safety interlock with active RPM > 0 | Rejects OTA_BEGIN with INVALID_STATE | **PENDING PHYSICAL BENCH TEST** |
| TC-03 | Safety interlock with active CAN traffic | Rejects OTA_BEGIN with INVALID_STATE | **PENDING PHYSICAL BENCH TEST** |
| TC-04 | Rejection of unsigned raw binary | Blocked by Android / rejected by ESP32 | **PENDING PHYSICAL BENCH TEST** |
| TC-05 | Rejection of binary with invalid ECDSA signature | Rejected by ESP32 signature check | **PENDING PHYSICAL BENCH TEST** |
| TC-06 | Rejection of corrupted CRC32 / bit-flip | Rejected by ESP32 CRC32 check | **PENDING PHYSICAL BENCH TEST** |
| TC-07 | Rejection of truncated transfer | Rejected by ESP32 byte count check | **PENDING PHYSICAL BENCH TEST** |
| TC-08 | Forced boot failure with automatic rollback | Rolls back to prior slot, Android detects rollback | **PENDING PHYSICAL BENCH TEST** |
| TC-09 | Power cut at 50% transfer | Prior slot boots unaffected, NVS and LittleFS intact | **PENDING PHYSICAL BENCH TEST** |
| TC-10 | Post-reboot Android confirmation handshake | No false success on timeout, disconnect or rollback | **PENDING PHYSICAL BENCH TEST** |
