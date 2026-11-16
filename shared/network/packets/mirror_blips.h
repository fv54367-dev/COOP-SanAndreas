#pragma once
// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0), package M2, and adapted.

#include <cstdint>
#include <cstdio>
#include <string>

// NOLINTBEGIN(readability-simplify-boolean-expr)

// The mission mirror layer's blips (client/src/CMirrorBlips.h; docs/design/mirror-layer.md 3.8 of project_gta_sa_coop,
// package M2). The script authority sends the radar blips its mission created, as long as it keeps them: what changed
// once per frame, and its whole set with bReset to a player who joins or goes back to coop (that player alone) and after
// its own game restarted (to everyone). The server relays it from the script authority only and fills in the sender, so
// the other games can drop what a former authority sent before it changed.
// Adapted: network ids are this fork's 64-bit packed ids (pedid_t / vehicleid_t) and player ids playerid_t; objects
// are not mirrored by id (no CMirrorObjects here), they travel as a blip at the authority's position.
namespace MirrorBlipsWire
{
constexpr uint8_t OP_FULL = 0;      // the blip's whole state: new or changed
constexpr uint8_t OP_REMOVED = 1;   // the authority's blip is gone
constexpr uint8_t OP_POSITION = 2;  // only its position, for a blip the receivers may show at the authority's position

constexpr uint8_t ENTITY_NONE = 0;     // a coordinate, or an entity without a network id
constexpr uint8_t ENTITY_PLAYER = 1;   // a player's ped, by playerid_t
constexpr uint8_t ENTITY_PED = 2;      // CNetworkPed id (pedid_t, packed)
constexpr uint8_t ENTITY_VEHICLE = 3;  // CNetworkVehicle id (vehicleid_t, packed)
constexpr uint8_t ENTITY_OBJECT = 4;   // CObjectSync mission object id

constexpr uint8_t TYPE_FIRST = 1;  // eBlipType BLIP_CAR
constexpr uint8_t TYPE_LAST = 8;   // eBlipType BLIP_AIRSTRIP

// entries per packet (at most about 30 bytes each); a bigger set goes out in several packets, bReset only on the first
constexpr int MAX_ENTRIES = 48;
}  // namespace MirrorBlipsWire

namespace Packets::Mirror
{
struct SMirrorBlipEntry
{
    uint32_t hostHandle = 0;  // the authority's radar handle, the blip's identity
    uint8_t op = MirrorBlipsWire::OP_FULL;
    WorldPositionCompressed position{};  // FULL and POSITION

    // FULL only
    uint8_t type = MirrorBlipsWire::TYPE_FIRST;  // eBlipType
    uint8_t entityKind = MirrorBlipsWire::ENTITY_NONE;
    uint64_t entityId = 0;  // with an entity kind: playerid_t value, or a packed pedid_t / vehicleid_t
    uint32_t colour = 0;
    uint8_t sprite = 0;   // eRadarSprite
    uint8_t display = 0;  // eBlipDisplay
    uint8_t size = 1;
    bool bBright = true;
    bool bShortRange = false;
    bool bFriendly = false;
    bool bRemain = false;
    bool bFade = false;
    uint8_t coordAppearance = 0;
    bool bEntrance = false;              // coordinate types: shown at an interior's entrance
    WorldPositionCompressed entrance{};  // the centre of that entrance

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint32(stream, hostHandle);
        serialize_int(stream, op, MirrorBlipsWire::OP_FULL, MirrorBlipsWire::OP_POSITION);
        if (op == MirrorBlipsWire::OP_REMOVED)
        {
            return true;
        }
        serialize_object(stream, position);
        if (op == MirrorBlipsWire::OP_POSITION)
        {
            return true;
        }

        serialize_int(stream, type, MirrorBlipsWire::TYPE_FIRST, MirrorBlipsWire::TYPE_LAST);
        serialize_int(stream, entityKind, MirrorBlipsWire::ENTITY_NONE, MirrorBlipsWire::ENTITY_OBJECT);
        switch (entityKind)
        {
            case MirrorBlipsWire::ENTITY_OBJECT:
            {
                uint32_t object = static_cast<uint32_t>(entityId);
                serialize_uint32(stream, object);
                entityId = object;
                break;
            }
            case MirrorBlipsWire::ENTITY_PLAYER:
            {
                uint32_t player = static_cast<uint32_t>(entityId);
                serialize_uint32(stream, player);
                entityId = player;
                break;
            }
            case MirrorBlipsWire::ENTITY_PED:
            case MirrorBlipsWire::ENTITY_VEHICLE:
                serialize_uint64(stream, entityId);
                // either fully invalid or fully valid (entityid_t::IsPackedWellFormed)
                if ((static_cast<uint32_t>(entityId >> 32) == 0) != (static_cast<uint32_t>(entityId) == 0))
                {
                    return false;
                }
                break;
            default:
                break;
        }
        serialize_uint32(stream, colour);
        serialize_int(stream, sprite, 0, 63);
        serialize_int(stream, display, 0, 3);
        serialize_int(stream, size, 0, 255);
        serialize_bool(stream, bBright);
        serialize_bool(stream, bShortRange);
        serialize_bool(stream, bFriendly);
        serialize_bool(stream, bRemain);
        serialize_bool(stream, bFade);
        serialize_int(stream, coordAppearance, 0, 3);
        serialize_bool(stream, bEntrance);
        if (bEntrance)
        {
            serialize_object(stream, entrance);
        }
        return true;
    }
};

class MirrorBlips : public Packet
{
    DEFINE_PACKET_TYPE(MirrorBlips, ePacketType::MISSION_MIRROR_BLIPS, ePacketChannel::SCRIPT);

public:
    SenderPlayerId playerid{};    // the script authority, filled in by the server
    playerid_t targetPlayerId{};  // invalid (0) = the other players of the mission, else that player alone (a snapshot)
    bool bReset = false;          // drop every mirrored blip before applying the entries
    int count = 0;
    SMirrorBlipEntry entries[MirrorBlipsWire::MAX_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        // the bit writer only asserts its buffer bound: a count out of range is refused before anything is written
        if (Stream::IsWriting && (count < 0 || count > MirrorBlipsWire::MAX_ENTRIES))
        {
            return false;
        }
        serialize_object(stream, playerid);
        serialize_playerid(stream, targetPlayerId);
        serialize_bool(stream, bReset);
        serialize_int(stream, count, 0, MirrorBlipsWire::MAX_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_object(stream, entries[i]);
        }
        return true;
    }

public:
    std::string ToString() const override
    {
        char text[48];
        snprintf(text, sizeof(text), "reset %d, target %u, %d entries", bReset ? 1 : 0, targetPlayerId.value(), count);
        return text;
    }
};
}  // namespace Packets::Mirror

// NOLINTEND(readability-simplify-boolean-expr)
