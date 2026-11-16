#pragma once
#include "network/packet.h"
#include "network/ids.h"
namespace Packets::Scripts
{
class MissionPhoto : public Packet
{
    DEFINE_PACKET_TYPE(MissionPhoto, ePacketType::MISSION_PHOTO, ePacketChannel::EVENT);
public:
    SenderPlayerId senderPlayerid{};
    playerid_t owner{};
    uint32_t epoch = 0;
    int mission = -1;
    bool request = false;
    bool object = false;
    uint32_t entity = 0;
    uint32_t shotAt = 0;
    pedid_t ped{};
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, owner);
        serialize_bits(stream, epoch, 32);
        serialize_int(stream, mission, -1, 135);
        serialize_bool(stream, request);
        serialize_bool(stream, object);
        serialize_bits(stream, entity, 32);
        serialize_bits(stream, shotAt, 32);
        serialize_pedid(stream, ped);
        return true;
    }
};
}
