#include <cassert>
#include <iostream>
#include "meckchat/protocol/framing.h"

using namespace MeckChat::Protocol;

void testFrameEncodingDecoding() {
    P2PFrame frame;
    frame.type = FrameType::ChatMessage;
    frame.payload = QByteArray("{\"content\":\"test framing message\"}");

    QByteArray encoded = frame.encode();
    assert(encoded.size() >= 12);

    auto decoded = P2PFrame::decode(encoded);
    assert(decoded.has_value());
    assert(decoded->type == FrameType::ChatMessage);
    assert(decoded->payload == frame.payload);

    std::cout << "[PASS] testFrameEncodingDecoding" << std::endl;
}

void testFrameCrcCorrupt() {
    P2PFrame frame;
    frame.type = FrameType::Heartbeat;
    frame.payload = QByteArray("payload");

    QByteArray encoded = frame.encode();
    // Corrupt one byte in payload
    encoded[10] = static_cast<char>(encoded[10] ^ 0xFF);

    auto decoded = P2PFrame::decode(encoded);
    assert(!decoded.has_value()); // Must reject due to CRC failure

    std::cout << "[PASS] testFrameCrcCorrupt" << std::endl;
}

int main() {
    testFrameEncodingDecoding();
    testFrameCrcCorrupt();
    std::cout << "All Linux Framing Tests Passed Successfully!" << std::endl;
    return 0;
}
