#pragma once
#include "network/packet.h"
#include "network/ids.h"

namespace Packets::Scripts
{
class MissionGuestCounts : public Packet
{
    DEFINE_PACKET_TYPE(MissionGuestCounts, ePacketType::MISSION_GUEST_COUNTS, ePacketChannel::SYNC);
public:
    SenderPlayerId senderPlayerid{};
    playerid_t owner{};
    uint32_t epoch = 0;
    int mission = -1;
    bool request = false;
    bool shooting = false;
    pedid_t target{};

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, owner);
        serialize_bits(stream, epoch, 32);
        serialize_int(stream, mission, -1, 135);
        serialize_bool(stream, request);
        serialize_bool(stream, shooting);
        serialize_pedid(stream, target);
        return true;
    }
};
}
