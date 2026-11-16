#pragma once

#include <cstdint>

// The mission mirror layer's pickups (client/src/CMirrorPickups.h). The script authority's mission creates a pickup
// (CREATE_PICKUP 0213 / CREATE_PICKUP_WITH_AMMO 032B): the other players of the mission get a copy. A player who
// collects his copy tells the authority, whose game marks its own pickup collected (the script's
// HAS_PICKUP_BEEN_COLLECTED sees it); a pickup gone on the authority is removed everywhere.
namespace MirrorPickupsWire
{
constexpr uint8_t OP_CREATE = 0;     // authority -> others: a new mission pickup
constexpr uint8_t OP_REMOVE = 1;     // authority -> others: its pickup is gone (collected, removed, mission over)
constexpr uint8_t OP_COLLECTED = 2;  // a player -> the authority: he collected his copy
}  // namespace MirrorPickupsWire

namespace Packets::Mirror
{
class MirrorPickup : public Packet
{
    DEFINE_PACKET_TYPE(MirrorPickup, ePacketType::MISSION_MIRROR_PICKUP, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};  // filled in by the server
    uint8_t op = MirrorPickupsWire::OP_CREATE;
    uint32_t hostHandle = 0;  // the authority's pickup handle (CPickups unique index), the pickup's identity
    uint16_t modelId = 0;
    uint8_t pickupType = 0;
    int32_t ammo = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, playerid);
        serialize_int(stream, op, MirrorPickupsWire::OP_CREATE, MirrorPickupsWire::OP_COLLECTED);
        serialize_uint32(stream, hostHandle);
        if (op == MirrorPickupsWire::OP_CREATE)
        {
            serialize_int(stream, modelId, 0, 19999);
            serialize_bits(stream, pickupType, 8);
            serialize_int(stream, ammo, 0, 99999);
            serialize_float(stream, x);
            serialize_float(stream, y);
            serialize_float(stream, z);
        }
        return true;
    }
};
}  // namespace Packets::Mirror
