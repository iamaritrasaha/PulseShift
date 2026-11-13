import asyncio
import hashlib
import struct
import time
import serial
from bleak import BleakClient

ESP32_ADDR = "B0:CB:D8:09:98:8E"

CHAR_CMD = "fe12d215-3d4f-4b9d-a21c-92782ecb4000"
CHAR_RESULT = "fe12d216-3d4f-4b9d-a21c-92782ecb4000"

# Opcodes
OP_SET_BRIGHTNESS    = 0x11
OP_SAVE_CONFIG       = 0x20
OP_ENROLL_OWNER      = 0x24
OP_AUTHENTICATE      = 0x25
OP_REQUEST_CHALLENGE = 0x26

STATUS_OK            = 0
STATUS_UNAUTHORIZED  = 4
STATUS_INVALID_STATE = 5

result_queue = asyncio.Queue()

def result_callback(sender, data):
    res_fmt = "<BBHBBIB"
    u = struct.unpack(res_fmt, data[:11])
    payload = data[11:]
    pkt = {
        "version": u[0],
        "opcode": u[1],
        "sequence": u[2],
        "status": u[3],
        "flags": u[4],
        "revision": u[5],
        "payloadLength": u[6],
        "payload": payload[:u[6]],
    }
    result_queue.put_nowait(pkt)

async def send_cmd(client, opcode, seq, payload=b"", flags=0):
    cmd_fmt = "<BBHBB14s"
    padded_payload = payload.ljust(14, b"\x00")
    data = struct.pack(cmd_fmt, 2, opcode, seq, len(payload), flags, padded_payload)
    await client.write_gatt_char(CHAR_CMD, data, response=False)
    return await asyncio.wait_for(result_queue.get(), timeout=2.5)

def trigger_boot_window():
    ser = serial.Serial()
    ser.port = 'COM5'
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.write(b'AUTH ON\r\n')
    ser.flush()
    time.sleep(0.1)
    ser.close()
    print("Physical BOOT window triggered (cyan pulse active on ESP32)")

def close_boot_window():
    ser = serial.Serial()
    ser.port = 'COM5'
    ser.baudrate = 115200
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.write(b'AUTH OFF\r\n')
    ser.flush()
    time.sleep(0.1)
    ser.close()
    print("Physical BOOT window closed via AUTH OFF")

async def run_full_flow():
    secret = b"PULSE_KEY_96" # Exactly 12 bytes!

    # 1. Trigger open BOOT window
    trigger_boot_window()
    await asyncio.sleep(0.5)

    print(f"\nConnecting to ESP32 at {ESP32_ADDR}...")
    async with BleakClient(ESP32_ADDR) as client:
        print("Connected!")
        await client.start_notify(CHAR_RESULT, result_callback)
        await asyncio.sleep(0.3)

        # 2. Enroll Owner with 12-byte secret
        print("\n--- Step 1: Enroll Owner Secret during open BOOT window ---")
        res_enroll = await send_cmd(client, OP_ENROLL_OWNER, 201, secret)
        print(f"Enroll Result: opcode=0x{res_enroll['opcode']:02X}, status={res_enroll['status']}, flags=0x{res_enroll['flags']:02X}")
        assert res_enroll['status'] == STATUS_OK, "Enrollment failed!"

        # Session is now authorized immediately post-enrollment
        print("\n--- Step 2: Write configuration while session is authorized ---")
        res_write = await send_cmd(client, OP_SET_BRIGHTNESS, 202, bytes([222]))
        print(f"Write Result: opcode=0x{res_write['opcode']:02X}, status={res_write['status']} (expected 0 OK)")
        assert res_write['status'] == STATUS_OK, "Write failed during authorized session!"

    # 3. Disconnect client to drop session and close the boot window explicitly
    print("\nClient disconnected. Closing physical BOOT window explicitly...")
    close_boot_window()
    await asyncio.sleep(1.0)

    # 4. Re-connect
    print(f"\nRe-connecting to test challenge-response flow without BOOT button...")
    async with BleakClient(ESP32_ADDR) as client:
        await client.start_notify(CHAR_RESULT, result_callback)
        await asyncio.sleep(0.3)

        # 5. Verify write is rejected without re-authentication
        print("\n--- Step 3: Write without authenticating fresh session ---")
        res_rej = await send_cmd(client, OP_SET_BRIGHTNESS, 301, bytes([180]))
        print(f"Write Result: opcode=0x{res_rej['opcode']:02X}, status={res_rej['status']} (expected 4 UNAUTHORIZED)")
        assert res_rej['status'] == STATUS_UNAUTHORIZED, "Write succeeded when it should be unauthorized!"

        # 6. Request single-use Challenge Nonce
        print("\n--- Step 4: Request Challenge Nonce (Opcode 0x26) ---")
        res_chal = await send_cmd(client, OP_REQUEST_CHALLENGE, 302)
        print(f"Challenge Result: opcode=0x{res_chal['opcode']:02X}, status={res_chal['status']}, payloadLen={res_chal['payloadLength']}")
        assert res_chal['status'] == STATUS_OK, "Challenge request failed!"
        assert res_chal['payloadLength'] == 4, "Challenge payload must be 4 bytes!"
        challenge = struct.unpack("<I", res_chal['payload'])[0]
        print(f"Received Nonce: 0x{challenge:08X}")

        # 7. Compute cryptographic proof over 12-byte secret + 4-byte challenge + 2-byte sequence
        seq = 303
        msg = secret + struct.pack("<IH", challenge, seq)
        proof = hashlib.sha256(msg).digest()[:8]
        print(f"Computed SHA-256 Proof for seq={seq}: {proof.hex()}")

        # 8. Authenticate Challenge (Opcode 0x25)
        print("\n--- Step 5: Authenticate with Proof (Opcode 0x25) ---")
        res_auth = await send_cmd(client, OP_AUTHENTICATE, seq, proof)
        print(f"Auth Result: opcode=0x{res_auth['opcode']:02X}, status={res_auth['status']} (expected 0 OK)")
        assert res_auth['status'] == STATUS_OK, "Challenge authentication failed!"

        # 9. Verify write now succeeds WITHOUT pressing BOOT button!
        print("\n--- Step 6: Write configuration as authenticated enrolled owner ---")
        res_write2 = await send_cmd(client, OP_SET_BRIGHTNESS, 304, bytes([240]))
        print(f"Write Result: opcode=0x{res_write2['opcode']:02X}, status={res_write2['status']} (expected 0 OK)")
        assert res_write2['status'] == STATUS_OK, "Authenticated owner write failed!"

        # 10. Test Replay Attack: Re-send same proof with sequence
        print("\n--- Step 7: Negative Replay Attack Test (Re-send same proof) ---")
        res_replay = await send_cmd(client, OP_AUTHENTICATE, seq, proof)
        print(f"Replay Result: opcode=0x{res_replay['opcode']:02X}, status={res_replay['status']} (expected 5 INVALID_STATE / challenge consumed)")
        assert res_replay['status'] == STATUS_INVALID_STATE or res_replay['status'] == STATUS_UNAUTHORIZED, "Replay attack was not rejected!"

        print("\nSUCCESS! COMPLETE OWNER ENROLLMENT AND CHALLENGE-RESPONSE AUTHENTICATION VERIFIED ON PHYSICAL ESP32!")

if __name__ == "__main__":
    asyncio.run(run_full_flow())
