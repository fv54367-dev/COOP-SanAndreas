#pragma once
#include "network/packet.h"
#include "network/ids.h"

namespace Packets::Scripts
{
// T67: the machine of the running mission that the guests get a seat of (the authority's turret or RC toy).
// Sent by the script authority while the mission runs, on change and as a heartbeat every 2 s (a guest drops
// the phase 5 s after the last one). kind NONE turns it off.
class MachinePhase : public Packet
{
    DEFINE_PACKET_TYPE(MachinePhase, ePacketType::MACHINE_PHASE, ePacketChannel::SCRIPT);

public:
    enum : uint8_t
    {
        NONE = 0,
        TURRET,     // ZERO1 "Air Raid": the owner's minigun turret on Zero's roof (its invisible base object)
        RC_BARON,   // ZERO2 "Supply Lines...": the owner's RC Baron, given on Zero's roof
        RC_GOBLIN,  // ZERO4 "New Model Army" (defined for later work: not emitted yet, the table is an interior)
        FORKLIFT,   // RYDER2 "Robbing Uncle Sam": the forklift of the depot (T70), crates move with the move channel
    };
    SenderPlayerId senderPlayerid{};
    playerid_t owner{};  // filled by the server: the script authority
    uint8_t kind = NONE;
    CVector pos{};      // where the guest's machine of its own goes (roof / beside the turret)
    float heading = 0;  // how the owner's machine faces
    uint8_t area = 0;   // the area the machine lives in (0: the street, 10: the war table)

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, owner);
        serialize_int(stream, kind, 0, 4);
        serialize_vector(stream, pos);
        serialize_float(stream, heading);
        serialize_int(stream, area, 0, 255);
        return kind <= FORKLIFT && std::isfinite(heading) && std::isfinite(pos.x) && std::isfinite(pos.y) &&
            std::isfinite(pos.z);
    }
};

// T67: what a guest's own machine did to the owner's shared mission vehicles (his minigun hollowed a plane of
// "Air Raid", a van of "Supply Lines..."). The guest reports the copy's health after his hits; the authority
// applies the difference to his own vehicle with the game's own CVehicle::InflictDamage, so the mission counts
// it natively, once. Rate-limited by the guest (150 ms per vehicle), health only goes down.
class GuestMachineDamage : public Packet
{
    DEFINE_PACKET_TYPE(GuestMachineDamage, ePacketType::GUEST_MACHINE_DAMAGE, ePacketChannel::SCRIPT);

public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};  // the shared mission vehicle the guest's copy stands for
    float health = 0;         // the guest's copy health after his hits (0: blown up)
    uint8_t weapon = 38;      // eWeaponType of the hits (the machines' guns are the minigun class)
    CVector pos{};            // where the last hit landed

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_float(stream, health);
        serialize_int(stream, weapon, 0, 64);
        serialize_vector(stream, pos);
        return std::isfinite(health) && health >= 0.0f && health <= 100000.0f && std::isfinite(pos.x) &&
            std::isfinite(pos.y) && std::isfinite(pos.z);
    }
};

// T70: where a guest's copy of a mission object now stands (a crate of "Robbing Uncle Sam" he is moving with
// his own machine). The authority applies it to the real object, so the mission counts natively; while the
// guest keeps reporting (150 ms at most), nobody else moves it, and a guest who stops or disconnects leaves the
// object where his last report put it.
class GuestObjectMove : public Packet
{
    DEFINE_PACKET_TYPE(GuestObjectMove, ePacketType::GUEST_OBJECT_MOVE, ePacketChannel::SCRIPT);

public:
    SenderPlayerId senderPlayerid{};
    uint32_t objectid = 0;  // CObjectSync id of the object
    CVector pos{};
    CVector right{};
    CVector forward{};  // CMatrix::up holds the forward axis, as in ObjectSync
    uint8_t area = 0;
    uint8_t released = 0;  // T75: the guest let go of it here (his last report): the real one falls from there

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_bits(stream, objectid, 32);
        serialize_vector(stream, pos);
        serialize_vector(stream, right);
        serialize_vector(stream, forward);
        serialize_int(stream, area, 0, 255);
        serialize_int(stream, released, 0, 1);
        return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) &&
            std::isfinite(right.x) && std::isfinite(right.y) && std::isfinite(right.z) &&
            std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z);
    }
};
}  // namespace Packets::Scripts
