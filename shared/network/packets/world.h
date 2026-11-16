#pragma once
#include "CWeather.h"
#include <CExplosion.h>

namespace Packets::World
{
// TODO: proper weather sync
class GameWeatherTime : public Packet
{
    DEFINE_PACKET_TYPE(GameWeatherTime, ePacketType::GAME_WEATHER_TIME, ePacketChannel::EVENT);

public:
    eWeatherType newWeather;
    eWeatherType oldWeather;
    // eWeatherType forcedWeather;
    uint8_t currentMonth;
    uint8_t currentDay;
    uint8_t currentHour;
    uint8_t currentMinute;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting)
        {
            newWeather = VCLAMP(newWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            oldWeather = VCLAMP(oldWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            // forcedWeather = VCLAMP(forcedWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
            currentMonth = VCLAMP(currentMonth, 1, 12);
            currentDay = VCLAMP(currentDay, 1, 31);
            currentHour = VCLAMP(currentHour, 0, 23);
            currentMinute = VCLAMP(currentMinute, 0, 59);
        }
        serialize_int(stream, (int&)newWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
        serialize_int(stream, (int&)oldWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);
        // serialize_int(stream, (int&)forcedWeather, WEATHER_EXTRASUNNY_LA, WEATHER_EXTRACOLOURS_2);

        serialize_int(stream, currentMonth, 1, 12);
        serialize_int(stream, currentDay, 1, 31);
        serialize_int(stream, currentHour, 0, 23);
        serialize_int(stream, currentMinute, 0, 59);

        return true;
    }
};

// unused for now
class AddExplosion : public Packet
{
    DEFINE_PACKET_TYPE(AddExplosion, ePacketType::ADD_EXPLOSION, ePacketChannel::EVENT);

public:
    eExplosionType type;
    WorldPositionCompressed pos;
    int time;
    bool usesSound;
    float cameraShake;
    bool isVisible;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, (int&)type, EXPLOSION_GRENADE, EXPLOSION_RC_VEHICLE);
        serialize_object(stream, pos);
        serialize_int(stream, time, 0, 100);
        serialize_bool(stream, usesSound);
        serialize_compressed_float(stream, cameraShake, -1.0f, 1.0f, 0.01f);
        serialize_bool(stream, isVisible);

        return true;
    }
};

class TagUpdate : public Packet
{
    DEFINE_PACKET_TYPE(TagUpdate, ePacketType::TAG_UPDATE, ePacketChannel::EVENT);

public:
    struct Payload
    {
        int16_t pos_x = 0;
        int16_t pos_y = 0;
        int16_t pos_z = 0;
        bool bFullySprayed = false;
        uint8_t alpha = 0;
    } payload;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, payload.pos_x, -3000, 3000);
        serialize_int(stream, payload.pos_y, -3000, 3000);
        serialize_int(stream, payload.pos_z, -3000, 3000);

        serialize_bool(stream, payload.bFullySprayed);
        if (payload.bFullySprayed)
        {
            payload.alpha = 255;
        }
        else
        {
            if (Stream::IsWriting)
            {
                uint8_t temp = payload.alpha / 8;
                serialize_int(stream, temp, 0, 31);
            }
            else if (Stream::IsReading)
            {
                uint8_t temp;
                serialize_int(stream, temp, 0, 31);
                payload.alpha = temp * 8;
            }
        }

        return true;
    }
};

// an oyster, horseshoe or snapshot the sender collected in coop mission mode (see CCollectibleSync)
class CollectiblePicked : public Packet
{
    DEFINE_PACKET_TYPE(CollectiblePicked, ePacketType::COLLECTIBLE_PICKED, ePacketChannel::EVENT);

public:
    playerid_t playerid{};  // filled by the server with the sender
    uint8_t kind = 0;       // 0 oyster, 1 horseshoe, 2 snapshot
    WorldPositionCompressed pos{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_int(stream, kind, 0, 7);
        serialize_object(stream, pos);
        return true;
    }
};

class UpdateAllTags : public Packet
{
    DEFINE_PACKET_TYPE(UpdateAllTags, ePacketType::UPDATE_ALL_TAGS, ePacketChannel::EVENT);

public:
    TagUpdate::Payload tags[100];

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        for (size_t i = 0; i < ARRAY_SIZE(tags); i++)
        {
            serialize_int(stream, tags[i].pos_x, -3000, 3000);
            serialize_int(stream, tags[i].pos_y, -3000, 3000);
            serialize_int(stream, tags[i].pos_z, -3000, 3000);
            bool bIsNull = false;
            bool bFullySprayed = false;
            if (Stream::IsWriting)
            {
                if (tags[i].alpha == 0)
                {
                    bIsNull = true;
                }
                serialize_bool(stream, bIsNull);
                if (bIsNull)
                {
                    continue;
                }

                if (tags[i].alpha == 255)
                {
                    bFullySprayed = true;
                }
                serialize_bool(stream, bFullySprayed);
                if (bFullySprayed)
                {
                    continue;
                }
                uint8_t temp = tags[i].alpha / 8;
                serialize_int(stream, temp, 0, 31);
            }
            else if (Stream::IsReading)
            {
                serialize_bool(stream, bIsNull);
                if (bIsNull)
                {
                    tags[i].alpha = 0;
                    continue;
                }

                serialize_bool(stream, bFullySprayed);
                if (bFullySprayed)
                {
                    tags[i].alpha = 255;
                    continue;
                }

                uint8_t temp;
                serialize_int(stream, temp, 0, 31);
                tags[i].alpha = temp * 8;
            }
        }
        return true;
    }
};

class UpdateMoonSize : public Packet
{
    DEFINE_PACKET_TYPE(UpdateMoonSize, ePacketType::UPDATE_MOON_SIZE, ePacketChannel::EVENT);

public:
    uint8_t moonSize;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, moonSize, 0, 7);
        return true;
    }
};

// a weapon or money dropped by a dead ped, mirrored on every client
class PickupSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PickupSpawn, ePacketType::PICKUP_SPAWN, ePacketChannel::EVENT);

public:
    uint32_t pickupid{};  // creator playerid << 24 | counter
    uint16_t modelId{};
    uint8_t pickupType{};
    int32_t ammo{};
    WorldPositionCompressed pos{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, pickupid, 32);
        serialize_int(stream, modelId, 0, 19999);
        serialize_int(stream, pickupType, 0, 30);
        serialize_int(stream, ammo, 0, 99999);
        serialize_object(stream, pos);
        return true;
    }
};

// a mission object of the host's coop mission: created, moved or changed (see CObjectSync)
class ObjectSync : public Packet
{
    DEFINE_PACKET_TYPE(ObjectSync, ePacketType::OBJECT_SYNC, ePacketChannel::EVENT);

public:
    uint32_t objectid{};
    uint16_t modelId{};
    WorldPositionCompressed pos{};
    NormalizedVector right{};
    NormalizedVector forward{};
    uint8_t areaCode{};
    bool visible = true;
    bool collision = true;
    bool broken = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, objectid, 32);
        serialize_int(stream, modelId, 0, 19999);
        serialize_object(stream, pos);
        serialize_object(stream, right);
        serialize_object(stream, forward);
        serialize_uint8(stream, areaCode);
        serialize_bool(stream, visible);
        serialize_bool(stream, collision);
        serialize_bool(stream, broken);
        return true;
    }
};

class ObjectRemove : public Packet
{
    DEFINE_PACKET_TYPE(ObjectRemove, ePacketType::OBJECT_REMOVE, ePacketChannel::EVENT);

public:
    uint32_t objectid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, objectid, 32);
        return true;
    }
};

class PickupRemove : public Packet
{
    DEFINE_PACKET_TYPE(PickupRemove, ePacketType::PICKUP_REMOVE, ePacketChannel::EVENT);

public:
    uint32_t pickupid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, pickupid, 32);
        return true;
    }
};

// a fire that started on the sender's client (see CFireSync)
class FireStart : public Packet
{
    DEFINE_PACKET_TYPE(FireStart, ePacketType::FIRE_START, ePacketChannel::EVENT);

public:
    uint32_t fireid{};  // owner playerid << 24 | counter
    WorldPositionCompressed pos{};
    float strength{};
    uint32_t remainingTime{};
    bool hasTarget{};
    CNetworkEntitySerializer target{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, fireid, 32);
        serialize_object(stream, pos);
        serialize_compressed_float(stream, strength, 0.0f, 10.0f, 0.01f);
        serialize_int(stream, remainingTime, 0, 600000);
        serialize_bool(stream, hasTarget);
        if (hasTarget)
        {
            serialize_object(stream, target);
        }
        return true;
    }
};

// extinguishRequest = false: the owner's fire ended, remove the copies
// extinguishRequest = true: a copy was put out, the owner should put the original out
class FireRemove : public Packet
{
    DEFINE_PACKET_TYPE(FireRemove, ePacketType::FIRE_REMOVE, ePacketChannel::EVENT);

public:
    uint32_t fireid{};
    bool extinguishRequest{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bits(stream, fireid, 32);
        serialize_bool(stream, extinguishRequest);
        return true;
    }
};

// the owner used his detonator: every client must blow up the copies of his satchel charges (see CWorld::UseDetonator
// in WorldHooks.cpp and DetonateSatchelsOf in PacketHandlers/world.cpp)
class DetonateProjectiles : public Packet
{
    DEFINE_PACKET_TYPE(DetonateProjectiles, ePacketType::DETONATE_PROJECTILES, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer creator{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, creator);
        return true;
    }
};

// a world cheat toggled by a player: index into CCheat::m_aCheatsActive (see CCheatSync)
class CheatSync : public Packet
{
    DEFINE_PACKET_TYPE(CheatSync, ePacketType::CHEAT_SYNC, ePacketChannel::EVENT);

public:
    uint8_t index{};
    bool active{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, index, 0, 91);
        serialize_bool(stream, active);
        return true;
    }
};

// a player used a cheat (typed or from the F8 menu): everybody shows "<name> used <cheat>" (any cheat, synced or not)
class CheatAnnounce : public Packet
{
    DEFINE_PACKET_TYPE(CheatAnnounce, ePacketType::CHEAT_ANNOUNCE, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    uint8_t index{};
    bool active{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, index, 0, 91);
        serialize_bool(stream, active);
        return true;
    }
};

// The name hash guards against a different garage list. Service doors are visual only.
class GarageDoor : public Packet
{
    DEFINE_PACKET_TYPE(GarageDoor, ePacketType::GARAGE_DOOR, ePacketChannel::EVENT);

public:
    uint8_t index{};
    uint32_t nameHash{};
    bool open{};
    bool visualOnly{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, index, 0, 63);
        serialize_bits(stream, nameHash, 32);
        serialize_bool(stream, open);
        serialize_bool(stream, visualOnly);
        return true;
    }
};

}  // namespace Packets::World
