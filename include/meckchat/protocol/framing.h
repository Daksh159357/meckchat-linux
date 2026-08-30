#pragma once

#include <QByteArray>
#include <cstdint>
#include <optional>

namespace MeckChat::Protocol {

constexpr uint16_t FRAME_MAGIC = 0x4D43; // 'M' 'C'

enum class FrameType : uint16_t {
    Heartbeat = 0x0001,
    ChatMessage = 0x0002,
    MessageAck = 0x0003,
    TypingIndicator = 0x0004,
    FileOffer = 0x0010,
    FileAccept = 0x0011,
    FileReject = 0x0012,
    FileChunk = 0x0013,
    FileComplete = 0x0014,
    FileCancel = 0x0015
};

struct P2PFrame {
    FrameType type{FrameType::Heartbeat};
    QByteArray payload;

    static uint32_t calculateCrc32(const char *data, size_t length);
    QByteArray encode() const;
    static std::optional<P2PFrame> decode(const QByteArray &data);
};

} // namespace MeckChat::Protocol
