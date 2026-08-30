#include "meckchat/protocol/framing.h"
#include <QtEndian>

namespace MeckChat::Protocol {

// Standard IEEE 802.3 CRC32 Implementation
uint32_t P2PFrame::calculateCrc32(const char *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = static_cast<uint8_t>(data[i]);
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}

QByteArray P2PFrame::encode() const {
    QByteArray packet;
    packet.reserve(2 + 2 + 4 + payload.size() + 4);

    // 1. Magic (2 bytes, big-endian)
    uint16_t magic = qToBigEndian(FRAME_MAGIC);
    packet.append(reinterpret_cast<const char*>(&magic), sizeof(magic));

    // 2. Type (2 bytes, big-endian)
    uint16_t typeRaw = qToBigEndian(static_cast<uint16_t>(type));
    packet.append(reinterpret_cast<const char*>(&typeRaw), sizeof(typeRaw));

    // 3. Payload Length (4 bytes, big-endian)
    uint32_t length = qToBigEndian(static_cast<uint32_t>(payload.size()));
    packet.append(reinterpret_cast<const char*>(&length), sizeof(length));

    // 4. Payload Data (N bytes)
    if (!payload.isEmpty()) {
        packet.append(payload);
    }

    // 5. CRC32 Checksum over [Magic + Type + Length + Payload]
    uint32_t crc = calculateCrc32(packet.constData(), packet.size());
    uint32_t crcBE = qToBigEndian(crc);
    packet.append(reinterpret_cast<const char*>(&crcBE), sizeof(crcBE));

    return packet;
}

std::optional<P2PFrame> P2PFrame::decode(const QByteArray &data) {
    // Minimum frame: 2 (Magic) + 2 (Type) + 4 (Len) + 0 (Payload) + 4 (CRC32) = 12 bytes
    if (data.size() < 12) return std::nullopt;

    const char *ptr = data.constData();

    // 1. Validate Magic
    uint16_t magic = qFromBigEndian(*reinterpret_cast<const uint16_t*>(ptr));
    if (magic != FRAME_MAGIC) return std::nullopt;

    // 2. Extract Type
    uint16_t typeVal = qFromBigEndian(*reinterpret_cast<const uint16_t*>(ptr + 2));

    // 3. Extract Length
    uint32_t payloadLen = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 4));
    if (data.size() != static_cast<int>(12 + payloadLen)) return std::nullopt;

    // 4. Verify CRC32
    uint32_t expectedCrc = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 8 + payloadLen));
    uint32_t calculatedCrc = calculateCrc32(ptr, 8 + payloadLen);
    if (expectedCrc != calculatedCrc) return std::nullopt;

    P2PFrame frame;
    frame.type = static_cast<FrameType>(typeVal);
    if (payloadLen > 0) {
        frame.payload = data.mid(8, payloadLen);
    }
    return frame;
}

} // namespace MeckChat::Protocol
