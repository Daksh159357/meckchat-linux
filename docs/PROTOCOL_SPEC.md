# MeckChat Universal Protocol Specification (v1.0)

**Standard Cross-Platform Specification for MeckChat Native Clients (Android, Linux, Windows)**

---

## 1. System Architecture & Transport Layers

MeckChat uses a **hybrid signaling + peer-to-peer data transport** model designed for absolute zero-knowledge and zero data-plane leakage:

```
[ Phase 1: Signaling & Discovery ]
  All Clients  ───► MQTT over TLS (Port 8883)  ───► HiveMQ Broker (broker.hivemq.com)
  (Zero chat or file payloads ever pass through MQTT)

[ Phase 2: Cryptographic Handshake & Pairing ]
  Client A ◄══════ Authenticated Key Exchange ══════► Client B
  (Argon2id + SHA-256 HMAC Proofs + Ephemeral Curve25519)

[ Phase 3: Direct Secure Data Transport ]
  Client A ◄──────────────── Encrypted WireGuard P2P ───────────────► Client B
  (Virtual Subnet 10.77.0.0/16, Port 51820 UDP + P2P Framing Socket Port 7788)
```

---

## 2. Device Identity

- **Device ID (`device_id`)**: String formatted as `mc_<uuidv4>` (e.g. `mc_f47ac10b-58cc-4372-a567-0e02b2c3d479`).
- **Platform Enum (`platform`)**: Lowercase string: `"android"` | `"linux"` | `"windows"`.
- **Display Name (`display_name`)**: User-configured UTF-8 string (1–64 chars).
- **Protocol Version (`protocol_version`)**: Integer `1`.

---

## 3. MQTT Signaling & Presence (Phase 1)

Broker: `broker.hivemq.com:8883` (TLS TCP).

### Topics

| Purpose | Topic Pattern | QoS | Retained |
| :--- | :--- | :--- | :--- |
| Online Presence | `meckchat/v1/presence/online/<device_id>` | 1 | `true` |
| Offline Presence (LWT) | `meckchat/v1/presence/offline/<device_id>` | 1 | `true` |
| Discovery Broadcast | `meckchat/v1/discovery` | 1 | `false` |
| Subscriptions | `meckchat/v1/presence/online/+`<br>`meckchat/v1/presence/offline/+`<br>`meckchat/v1/discovery` | 1 | N/A |

### Payloads

#### 1. `presence_online`
```json
{
  "type": "presence_online",
  "protocol_version": 1,
  "device_id": "mc_f47ac10b-58cc-4372-a567-0e02b2c3d479",
  "display_name": "Linux Workstation",
  "platform": "linux",
  "timestamp": 1725000000
}
```

#### 2. `presence_offline`
```json
{
  "type": "presence_offline",
  "device_id": "mc_f47ac10b-58cc-4372-a567-0e02b2c3d479"
}
```

#### 3. `discovery_request`
```json
{
  "type": "discovery_request",
  "protocol_version": 1,
  "device_id": "mc_f47ac10b-58cc-4372-a567-0e02b2c3d479",
  "timestamp": 1725000000
}
```

---

## 4. Cryptographic Pairing & Key Exchange (Phase 2)

Pairing is conducted using a user-shared secret or QR scan code:
1. **Key Generation**: Each device generates an ephemeral Curve25519 keypair for WireGuard: `(privKey, pubKey)`.
2. **Authentication Proof**:
   ```
   proof = HMAC-SHA256(
     key = Argon2id(shared_secret, salt),
     msg = sender_device_id || receiver_device_id || timestamp || sender_wg_public_key
   )
   ```
3. **Replay & Expiration Defense**:
   - `|current_time - timestamp| <= 300 seconds`
   - Nonces / request IDs must be unique per session.
4. **WireGuard Parameters**:
   - Sender exchanges its WireGuard Public Key (Base64) and proposed Virtual IP (`10.77.x.x/16`).

---

## 5. P2P Binary Packet Framing (Phase 3)

Direct P2P socket communication over the WireGuard interface (`10.77.0.0/16`) uses a strict binary framing protocol:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Magic: 'M' 'C'          |          Frame Type           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Payload Length                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Payload Data...                        |
|                           (N bytes)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        CRC-32 Checksum                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Field Definitions:
- **Magic Bytes** (2 bytes): `0x4D 0x43` (`'M'`, `'C'`).
- **Frame Type** (2 bytes, big-endian uint16):
  - `0x0001`: `HEARTBEAT`
  - `0x0002`: `CHAT_MESSAGE` (UTF-8 JSON)
  - `0x0003`: `MESSAGE_ACK` (UTF-8 JSON)
  - `0x0004`: `TYPING_INDICATOR` (UTF-8 JSON)
  - `0x0010`: `FILE_TRANSFER_OFFER` (UTF-8 JSON)
  - `0x0011`: `FILE_TRANSFER_ACCEPT` (UTF-8 JSON)
  - `0x0012`: `FILE_TRANSFER_REJECT` (UTF-8 JSON)
  - `0x0013`: `FILE_CHUNK` (Binary chunk header + bytes)
  - `0x0014`: `FILE_COMPLETE` (UTF-8 JSON SHA-256 verification)
  - `0x0015`: `FILE_CANCEL` (UTF-8 JSON)
- **Payload Length** (4 bytes, big-endian uint32): Byte count of Payload Data (max 4 MB per frame).
- **Payload Data** (`N` bytes).
- **CRC-32 Checksum** (4 bytes, big-endian uint32): IEEE 802.3 CRC32 computed over `[Magic + Type + Length + Payload]`.

---

## 6. Message Schemas

### 1. Chat Message (`0x0002`)
```json
{
  "message_id": "msg_8f14e45f-79bc-4b1f-9b2e-74dfa98e2190",
  "sender_device_id": "mc_f47ac10b-58cc-4372-a567-0e02b2c3d479",
  "recipient_device_id": "mc_6ba7b810-9dad-11d1-80b4-00c04fd430c8",
  "content": "Hello from native Linux to native Windows!",
  "timestamp": 1725000000,
  "reply_to_message_id": null
}
```

### 2. Message ACK (`0x0003`)
```json
{
  "message_id": "msg_8f14e45f-79bc-4b1f-9b2e-74dfa98e2190",
  "status": "delivered" | "read",
  "timestamp": 1725000001
}
```

---

## 7. File Transfer Protocol (FTP-over-P2P)

1. **Offer (`0x0010`)**:
   ```json
   {
     "transfer_id": "ft_1a2b3c4d",
     "file_name": "document.pdf",
     "file_size": 1048576,
     "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
     "chunk_size": 65536,
     "total_chunks": 16
   }
   ```
2. **Accept (`0x0011`)** / **Reject (`0x0012`)**:
   ```json
   { "transfer_id": "ft_1a2b3c4d" }
   ```
3. **Chunk Stream (`0x0013`)**:
   - Binary chunk header: `[Transfer ID (16B UUID)][Chunk Index (4B uint32)][Chunk Size (4B uint32)][Raw Bytes...]`
4. **Completion (`0x0014`)**:
   - Receiver reassembles chunks, verifies full file SHA-256 against offer hash, and emits acknowledgement.

---

## 8. Connection State Machine

```
   [ DISCONNECTED ]
          │ (MQTT Online)
          ▼
   [ DISCOVERED ]
          │ (User Initiates Pairing)
          ▼
   [ PAIRING_HANDSHAKE ]
          │ (Argon2id + HMAC Proofs Validated)
          ▼
   [ ESTABLISHING_TUNNEL ]
          │ (WireGuard Interface Configured & Up)
          ▼
     [ CONNECTED ]
     (P2P Framing Active: Chat, File Transfer, Typing)
```

---

## 9. Error Codes

| Code | Name | Description |
| :--- | :--- | :--- |
| `1001` | `ERR_PROTOCOL_MISMATCH` | Incompatible protocol versions |
| `1002` | `ERR_AUTH_FAILED` | Invalid pairing proof or password |
| `1003` | `ERR_AUTH_EXPIRED` | Pairing request timestamp expired (>300s) |
| `1004` | `ERR_TUNNEL_FAILED` | WireGuard interface creation / binding error |
| `1005` | `ERR_CRC_MISMATCH` | Frame CRC32 checksum failed |
| `1006` | `ERR_FILE_INTEGRITY` | Assembled file SHA-256 mismatch |
| `1007` | `ERR_PEER_OFFLINE` | Peer unannounced or unreachable |

---

## 10. Version Compatibility Rules

- Minor backward-compatible protocol increments increment the minor version (`1.1`).
- Breaking changes require incrementing `protocol_version` (`2.0`).
- Clients must gracefully reject unknown frame types without closing the underlying connection.
