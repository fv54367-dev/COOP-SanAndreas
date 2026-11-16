#pragma once

#include <CRadar.h>

namespace Packets::Blips
{
class UpdateEntityBlip : public Packet
{
    DEFINE_PACKET_TYPE(UpdateEntityBlip, ePacketType::UPDATE_ENTITY_BLIP, ePacketChannel::EVENT);

public:
    playerid_t forWhoPlayerId{}; // TODO(v0.3.1-alpha): send C2S only
    CNetworkEntitySerializer entity{};
    bool isFriendly = false;
    eBlipColour color = BLIP_COLOUR_RED;
    eBlipDisplay display = BLIP_DISPLAY_NEITHER;
    uint8_t scale = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        serialize_object(stream, entity);
        serialize_bool(stream, isFriendly);
        serialize_int(stream, (int&)color, BLIP_COLOUR_RED, BLIP_COLOUR_DESTINATION);
        serialize_int(stream, (int&)display, BLIP_DISPLAY_NEITHER, BLIP_DISPLAY_BOTH);
        serialize_uint8(stream, scale);
        return true;
    }
};

class RemoveEntityBlip : public Packet
{
    DEFINE_PACKET_TYPE(RemoveEntityBlip, ePacketType::REMOVE_ENTITY_BLIP, ePacketChannel::EVENT);

public:
    playerid_t forWhoPlayerId{}; // TODO(v0.3.1-alpha): send C2S only
    CNetworkEntitySerializer entity{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        serialize_object(stream, entity);
        return true;
    }
};

class ClearEntityBlips : public Packet
{
    DEFINE_PACKET_TYPE(ClearEntityBlips, ePacketType::CLEAR_ENTITY_BLIPS, ePacketChannel::EVENT);

public:
    playerid_t forWhoPlayerId{}; // TODO(v0.3.1-alpha): send C2S only

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        return true;
    }
};

class UpdateCheckpoint : public Packet
{
    DEFINE_PACKET_TYPE(UpdateCheckpoint, ePacketType::UPDATE_CHECKPOINT, ePacketChannel::EVENT);

public:
    playerid_t forWhoPlayerId{}; // TODO(v0.3.1-alpha): send C2S only
    WorldPositionCompressed position{};
    WorldPositionCompressed radius{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        serialize_object(stream, position);
        serialize_object(stream, radius);
        return true;
    }
};

class RemoveCheckpoint : public Packet
{
    DEFINE_PACKET_TYPE(RemoveCheckpoint, ePacketType::REMOVE_CHECKPOINT, ePacketChannel::EVENT);

public:
    playerid_t forWhoPlayerId{}; // TODO(v0.3.1-alpha): send C2S only

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        return true;
    }
};

// the area a mission script highlights this frame (CTheScripts::HighlightImportantArea 0x485E00: the red cylinder of
// LOCATE_* / IS_CHAR_IN_AREA_* with the sphere flag), sent by the script authority while it is drawn (CMissionAreaMarkers)
class MissionAreaMarker : public Packet
{
    DEFINE_PACKET_TYPE(MissionAreaMarker, ePacketType::MISSION_AREA_MARKER, ePacketChannel::SYNC);

public:
    uint32_t id = 0; // script + IP on the sender: only a key
    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    float z = 0.0f; // -100: on the ground

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint32(stream, id);
        serialize_float(stream, fromX);
        serialize_float(stream, fromY);
        serialize_float(stream, toX);
        serialize_float(stream, toY);
        serialize_float(stream, z);
        return true;
    }
};

// the race checkpoints of the script authority's synced mission (CMirrorRaceCheckpoints), the whole table each time
class MissionRaceCheckpoints : public Packet
{
    DEFINE_PACKET_TYPE(MissionRaceCheckpoints, ePacketType::MISSION_RACE_CHECKPOINTS, ePacketChannel::EVENT);

public:
    static constexpr int MAX_CHECKPOINTS = 20; // CTheScripts::ScriptCheckpointArray

    struct Entry
    {
        bool used = false;
        uint8_t type = 0; // eCheckpointType 0..8
        CVector pos{};
        CVector dir{};
        float size = 0.0f;
    };
    Entry entries[MAX_CHECKPOINTS]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        for (Entry& entry : entries)
        {
            serialize_bool(stream, entry.used);
            if (!entry.used)
                continue;
            serialize_int(stream, entry.type, 0, 15);
            serialize_float(stream, entry.pos.x);
            serialize_float(stream, entry.pos.y);
            serialize_float(stream, entry.pos.z);
            serialize_float(stream, entry.dir.x);
            serialize_float(stream, entry.dir.y);
            serialize_float(stream, entry.dir.z);
            serialize_float(stream, entry.size);
        }
        return true;
    }
};

struct _StaticBlipPayload
{
    WorldPositionCompressed position{};
    uint8_t sprite : 6; // eRadarSprite
    uint8_t display : 2; // eBlipDisplay
    uint8_t type : 1;  // 0 - BLIP_CONTACT_POINT, 1 - BLIP_COORD
    uint8_t missionBlip : 1; // bool: created by the host's synced mission (not sent to late/individual players)
    uint8_t shortRange : 1; // bool
    uint8_t friendly : 1;   // bool
    uint8_t coordBlipAppearance : 2;  // eCoordBlipAppearance
    uint8_t size : 3; 
    uint8_t color : 4; // eBlipColour
    
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, position);
        serialize_int(stream, sprite, RADAR_SPRITE_NONE, RADAR_SPRITE_COUNT - 1);
        serialize_int(stream, display, BLIP_DISPLAY_NEITHER, BLIP_DISPLAY_BOTH);
        serialize_int(stream, type, 0, 1);
        serialize_bool(stream, missionBlip);
        serialize_bool(stream, shortRange);
        serialize_bool(stream, friendly);
        serialize_int(stream, coordBlipAppearance, COORD_BLIP_APPEARANCE_NORMAL, COORD_BLIP_APPEARANCE_ENEMY);
        serialize_int(stream, size, 0, 7);
        serialize_int(stream, color, BLIP_COLOUR_RED, BLIP_COLOUR_DESTINATION);
        return true;
    }
};

class StaticBlipsSnapshot : public Packet
{
    DEFINE_PACKET_TYPE(StaticBlipsSnapshot, ePacketType::CREATE_STATIC_BLIP, ePacketChannel::EVENT);

public:
    uint8_t countBlips = 0;
    _StaticBlipPayload blips[MAX_RADAR_TRACES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, countBlips, 0, MAX_RADAR_TRACES - 1);
        for (size_t i = 0; i < countBlips; i++)
        {
            serialize_object(stream, blips[i]);
        }
        return true;
    }
};
}  // namespace Packets::Blips
