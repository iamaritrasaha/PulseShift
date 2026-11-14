import asyncio
import hashlib
import struct
from bleak import BleakClient

ESP32_ADDR = "B0:CB:D8:09:98:8E"

CHAR_CMD = "fe12d215-3d4f-4b9d-a21c-92782ecb4000"
CHAR_RESULT = "fe12d216-3d4f-4b9d-a21c-92782ecb4000"
CHAR_CAPABILITIES = "fe12d218-3d4f-4b9d-a21c-92782ecb4000"

# Opcodes
OP_GET_CAPABILITIES  = 0x01
OP_SET_BRIGHTNESS    = 0x11
OP_ENROLL_OWNER      = 0x24
OP_AUTHENTICATE      = 0x25
OP_REQUEST_CHALLENGE = 0x26

# Status codes
STATUS_OK            = 0
STATUS_UNAUTHORIZED  = 4
STATUS_INVALID_STATE = 5
STATUS_BUSY          = 3

result_queue = asyncio.Queue()

def result_callback(sender, data):
    # struct: <BBHBBIB9s
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
        "raw": data
    }
    result_queue.put_nowait(pkt)

async def send_cmd(client, opcode, seq, payload=b"", flags=0):
    cmd_fmt = "<BBHBB14s"
    padded_payload = payload.ljust(14, b"\x00")
    data = struct.pack(cmd_fmt, 2, opcode, seq, len(payload), flags, padded_payload)
    await client.write_gatt_char(CHAR_CMD, data, response=False)
    # wait for result notification
    try:
        res = await asyncio.wait_for(result_queue.get(), timeout=2.0)
        return res
    except asyncio.TimeoutError:
        return None

async def run_test():
    print(f"Connecting to ESP32 at {ESP32_ADDR}...")
    async with BleakClient(ESP32_ADDR) as client:
        print("Connected!")
        await client.start_notify(CHAR_RESULT, result_callback)
        await asyncio.sleep(0.5)

        # 1. Read Capabilities
        cap_bytes = await client.read_gatt_char(CHAR_CAPABILITIES)
        u_cap = struct.unpack("<BBIIBB8s", cap_bytes)
        print(f"Capabilities: version={u_cap[0]}, leds={u_cap[1]}, mask=0x{u_cap[2]:04X}, rev={u_cap[3]}, secMode={u_cap[5]}")
        assert u_cap[5] == 3, f"Expected securityMode=3 (HYBRID), got {u_cap[5]}"

        # 2. Test Negative: Attempt write without authorization outside BOOT window
        print("\n--- Test 1: Write without authorization ---")
        res1 = await send_cmd(client, OP_SET_BRIGHTNESS, 101, bytes([150]))
        print(f"Result: opcode=0x{res1['opcode']:02X}, status={res1['status']} (expected 4 UNAUTHORIZED)")
        assert res1['status'] == STATUS_UNAUTHORIZED, f"Expected STATUS_UNAUTHORIZED, got {res1['status']}"

        # 3. Test Negative: Attempt arbitrary fake token auth outside BOOT window
        print("\n--- Test 2: Fake token auth outside BOOT window ---")
        res2 = await send_cmd(client, OP_AUTHENTICATE, 102, b"FAKE_TOK")
        print(f"Result: opcode=0x{res2['opcode']:02X}, status={res2['status']} (expected 4 UNAUTHORIZED)")
        assert res2['status'] == STATUS_UNAUTHORIZED, f"Expected STATUS_UNAUTHORIZED, got {res2['status']}"

        # 4. Test Negative: Attempt challenge request when un-enrolled
        print("\n--- Test 3: Challenge request when un-enrolled ---")
        res3 = await send_cmd(client, OP_REQUEST_CHALLENGE, 103)
        print(f"Result: opcode=0x{res3['opcode']:02X}, status={res3['status']} (expected 5 INVALID_STATE / un-enrolled)")
        assert res3['status'] == STATUS_INVALID_STATE, f"Expected STATUS_INVALID_STATE, got {res3['status']}"

        # 5. Test Negative: Attempt enrollment outside BOOT window
        print("\n--- Test 4: Enrollment outside BOOT window ---")
        secret = b"PULSE_OWNER_KEY!" # 16 bytes
        res4 = await send_cmd(client, OP_ENROLL_OWNER, 104, secret)
        print(f"Result: opcode=0x{res4['opcode']:02X}, status={res4['status']} (expected 4 UNAUTHORIZED)")
        assert res4['status'] == STATUS_UNAUTHORIZED, f"Expected STATUS_UNAUTHORIZED, got {res4['status']}"

        print("\nALL IN-BAND BLE SECURITY NEGATIVE TESTS PASSED CONFIRMED ON ESP32 HARDWARE!")

if __name__ == "__main__":
    asyncio.run(run_test())
