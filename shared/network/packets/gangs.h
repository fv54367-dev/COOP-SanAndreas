#pragma once

#include "network/packet.h"
#include "network/packet_types.h"
#include "network/serializable_types.h"
#include "serialize.h"

// Territories (CZoneInfo) and gang wars are simulated by the host only (see CGangSync).
//
// Verified in gta_sa.exe 1.0 US - do NOT use CTheZones::ZoneInfoArray from the plugin-sdk, it points to a
// different array:
//   int16 CTheZones::TotalNumberOfZoneInfos = 0xBA1DE8
//   CZoneInfo CTheZones::ZoneInfoArray[380] = 0xBA1DF0 (step 0x11 bytes)
namespace Packets::Gangs
{
// CZoneInfo, 0x11 bytes (serialized as raw bytes so both sides just memcpy it):
//   +0x0  uint8 gangDensity[10]  gang influence of the zone (GANG_BALLAS..GANG_UNUSED2)
//   +0xA  uint8 dealerStrength
//   +0xB  uint8 colour[4]        RGBA the radar paints the zone with
//   +0xF  uint8 flags            bits 0-4 population type, bits 5-6 radar mode (1 = normal, 2 = attack), bit 7 no cops
//   +0x10 uint8 populationRaces
inline constexpr int ZONE_INFO_SIZE = 0x11;

// entries the game keeps in CTheZones::ZoneInfoArray
inline constexpr int MAX_ZONE_INFOS = 380;

// entries per GANG_ZONES_SYNC packet, a full resync may need more than one packet
inline constexpr int MAX_ZONE_ENTRIES = 128;

// one territory: index into the host's CZoneInfo array plus the whole 17 byte entry
class GangZoneEntry
{
public:
    int16_t index = 0;
    uint8_t data[ZONE_INFO_SIZE]{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, index, 0, MAX_ZONE_INFOS - 1);
        serialize_bytes(stream, data, ZONE_INFO_SIZE);
        return true;
    }
};

// host -> clients: sparse list of the territories with gang influence (densities, colour, flags) plus the
// host's CGangWars globals. `reset` marks the first packet of a full resync.
class GangZonesSync : public Packet
{
    DEFINE_PACKET_TYPE(GangZonesSync, ePacketType::GANG_ZONES_SYNC, ePacketChannel::EVENT);

public:
    bool reset = false;
    bool gangWarsActive = false;   // CGangWars::bGangWarsActive
    uint8_t numSpecificZones = 0;  // CGangWars::NumSpecificZones (0..6)
    int16_t specificZones[6]{};    // CGangWars::aSpecificZones (navigation zone indices)
    uint8_t count = 0;
    GangZoneEntry entries[MAX_ZONE_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, reset);
        serialize_bool(stream, gangWarsActive);
        serialize_int(stream, numSpecificZones, 0, 6);

        for (int i = 0; i < 6; i++)
        {
            int16_t zone = 0;
            if (Stream::IsWriting && i < numSpecificZones)
            {
                zone = specificZones[i];
            }

            serialize_int(stream, zone, 0, MAX_ZONE_INFOS - 1);

            if (Stream::IsReading)
            {
                specificZones[i] = i < numSpecificZones ? zone : -1;
            }
        }

        serialize_int(stream, count, 0, MAX_ZONE_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_object(stream, entries[i]);
        }

        return true;
    }
};

// host -> clients: current gang war (CGangWars). Clients do not simulate it (their CGangWars::Update call
// is skipped), they only mirror the notice, the attack blip and the attacked zone.
class GangWarState : public Packet
{
    DEFINE_PACKET_TYPE(GangWarState, ePacketType::GANG_WAR_STATE, ePacketChannel::EVENT);

public:
    bool active = false;                      // CGangWars::GangWarGoingOn()
    uint8_t state = 0;                        // CGangWars::State (eGangWarState)
    uint8_t attackState = 0;                  // CGangWars::State2 (eGangAttackState)
    uint8_t gang1 = 0;                        // CGangWars::Gang1 (eGangID), picks the blip colour/sprite
    int16_t zoneIndex = -1;                   // index of CGangWars::pZoneInfoToFightOver in CZoneInfo array
    WorldPositionCompressed pointOfAttack{};  // CGangWars::PointOfAttack, where the blip goes
    uint32_t fightTimer = 0;                  // CGangWars::FightTimer, only used to blink the blip

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, active);
        serialize_int(stream, state, 0, 6);
        serialize_int(stream, attackState, 0, 2);
        serialize_int(stream, gang1, 0, 11);
        serialize_int(stream, zoneIndex, -1, MAX_ZONE_INFOS - 1);
        serialize_object(stream, pointOfAttack);
        serialize_uint32(stream, fightTimer);
        return true;
    }
};
}  // namespace Packets::Gangs
