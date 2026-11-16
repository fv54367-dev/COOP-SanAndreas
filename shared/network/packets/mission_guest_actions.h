#pragma once
#include "network/packet.h"
#include "network/ids.h"
namespace Packets::Scripts
{
class MissionGuestActions : public Packet
{
    DEFINE_PACKET_TYPE(MissionGuestActions, ePacketType::MISSION_GUEST_ACTIONS, ePacketChannel::EVENT);
public:
    enum Kind { SESSION, INPUT, BEGIN, END, THROW, READY };
    SenderPlayerId senderPlayerid{};
    playerid_t owner{}, actor{};
    uint32_t epoch = 0, sequence = 0;
    int mission = -1, kind = SESSION, action = 0;
    bool pressed = false, crouched = false;
    float x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0, power = 0;
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, owner); serialize_playerid(stream, actor);
        serialize_bits(stream, epoch, 32); serialize_bits(stream, sequence, 32);
        serialize_int(stream, mission, -1, 135);
        serialize_int(stream, kind, SESSION, READY); serialize_int(stream, action, 0, 2);
        serialize_bool(stream, pressed); serialize_bool(stream, crouched);
        serialize_float(stream, x); serialize_float(stream, y); serialize_float(stream, z);
        serialize_float(stream, dx); serialize_float(stream, dy); serialize_float(stream, dz);
        serialize_float(stream, power);
        return true;
    }
};
}
