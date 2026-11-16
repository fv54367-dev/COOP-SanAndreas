#pragma once
#include "network/packet.h"
#include "network/ids.h"

namespace Packets::Scripts
{
class HomeInvasion : public Packet
{
    DEFINE_PACKET_TYPE(HomeInvasion, ePacketType::HOME_INVASION, ePacketChannel::SYNC);
public:
    SenderPlayerId senderPlayerid{};
    playerid_t owner{};
    uint32_t epoch = 0, sequence = 0;
    bool snapshot = false, active = false, mask = false, house = false;
    uint32_t noiseTotal = 0;
    playerid_t holders[7]{};  // per crate, so each player can carry his own (T91)
    uint32_t crate = 0, crates[7]{};
    uint8_t liftable = 0;
    bool request = false, holding = false, enter = false;
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, owner);
        serialize_bits(stream, epoch, 32);
        serialize_bits(stream, sequence, 32);
        serialize_bool(stream, snapshot);
        serialize_bool(stream, active);
        serialize_bool(stream, mask);
        serialize_bool(stream, house);
        serialize_bits(stream, noiseTotal, 32);
        for (auto& id : holders) serialize_playerid(stream, id);
        serialize_bits(stream, crate, 32);
        for (auto& id : crates) serialize_bits(stream, id, 32);
        serialize_bits(stream, liftable, 7);
        serialize_bool(stream, request);
        serialize_bool(stream, holding);
        serialize_bool(stream, enter);
        return true;
    }
};
}
