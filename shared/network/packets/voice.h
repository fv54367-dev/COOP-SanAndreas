#pragma once
#include "../packet.h"
#include "../ids.h"
namespace Packets::Voice
{
class Settings : public Packet
{
    DEFINE_PACKET_TYPE(Settings, ePacketType::VOICE_SETTINGS, ePacketChannel::SYSTEM);
public:
    bool enabled = false;
    uint16_t range = 50;
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_bool(stream, enabled);
        serialize_int(stream, range, 1, 500);
        return true;
    }
};
class Frame : public Packet
{
    DEFINE_PACKET_TYPE(Frame, ePacketType::VOICE_FRAME, ePacketChannel::VOICE);
public:
    playerid_t playerid{};
    uint16_t sequence = 0;
    uint8_t volume = 255;
    uint8_t data[164]{}; // 320 samples at 16 kHz; independent IMA ADPCM state.
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_uint16(stream, sequence);
        serialize_uint8(stream, volume);
        serialize_bytes(stream, data, sizeof(data));
        return data[2] <= 88 && data[3] == 0;
    }
};
}
