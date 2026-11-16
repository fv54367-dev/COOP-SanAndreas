#pragma once

#include "CPad.h"
#include "CVector.h"
#include "CWeapon.h"
#include "eCamMode.h"
#include "config.h"
#include "eWeaponType.h"
#include "network/packet.h"
#include "math_utils.h"
#include "network/serializable_types.h"
#include "serialize.h"
#include "eTaskType.h"
#include <CGame.h>
#include <network/CNetworkEntitySerializer.h>
#include <network/eNetworkEntityType.h>
#include <ePedBones.h>
#include <CPedClothesDesc.h>

// NOLINTBEGIN(readability-simplify-boolean-expr)
// NOLINTBEGIN(readability-isolate-declaration)

namespace Packets::Players
{
struct SKeySnapshot
{
public:
    CControllerStateCompressed newControllerState;
    CControllerStateCompressed oldControllerState;
    // the sender's controls are disabled (cutscene, minigame...): the states are the raw pad, they must not move
    // the remote ped, but the host's minigames can read them (see CNetworkPadSync)
    bool controlsDisabled = false;

public:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, newControllerState);
        serialize_object(stream, oldControllerState);
        serialize_bool(stream, controlsDisabled);
        return true;
    }
};

struct SWeaponSnapshot
{
public:
    uint8_t iWeaponType = WEAPON_UNARMED;
    uint8_t iWeaponState = WEAPONSTATE_READY;
    uint16_t nAmmo = 1;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, iWeaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_int(stream, iWeaponState, WEAPONSTATE_READY, WEAPONSTATE_MELEE_MADECONTACT);
        bool melee = false;
        if ((iWeaponType >= WEAPON_UNARMED && iWeaponType <= WEAPON_CANE) || iWeaponType == WEAPON_NIGHTVISION ||
            iWeaponType == WEAPON_INFRARED || iWeaponType == WEAPON_PARACHUTE)
        {
            melee = true;
        }
        if (melee)
        {
            if (Stream::IsReading)
            {
                nAmmo = 1;
            }
        }
        else
        {
            serialize_uint16(stream, nAmmo);
        }
        return true;
    }
};

struct SHealthSnapshot
{
public:
    uint8_t iHealth = 100;
    uint8_t iArmour = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, iHealth);
        bool hasArmour = false;
        if (Stream::IsWriting)
        {
            if (iArmour != 0)
            {
                hasArmour = true;
            }
        }
        serialize_bool(stream, hasArmour);
        if (hasArmour)
        {
            serialize_uint8(stream, iArmour);
        }
        else
        {
            iArmour = 0;
        }
        return true;
    }
};

class OnFootUpdate : public Packet
{
    DEFINE_PACKET_TYPE(OnFootUpdate, ePacketType::PLAYER_ONFOOT_UPDATE, ePacketChannel::SYNC);

public:
    SenderPlayerId senderPlayerid{};
    WorldPositionCompressed vecPos{};
    MoveSpeedCompressed vecMoveSpeed{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed aimingRotation{};
    SHealthSnapshot healthSnapshot{};
    SWeaponSnapshot weaponSnapshot{};
    bool bDucking = false;
    bool bHasJetpack = false;
    int8_t iFightingStyle{};
    SKeySnapshot keySnapshot{};
    uint8_t breathPercent = 100;  // air left under water, shown on the name tag
    uint8_t areaCode = 0;         // interior area of the player ped (0 = main map), used for the interior ownership

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);

        serialize_object(stream, vecPos);
        serialize_object(stream, vecMoveSpeed);

        serialize_object(stream, currentRotation);
        serialize_object(stream, aimingRotation);

        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);

        serialize_bool(stream, bDucking);
        serialize_bool(stream, bHasJetpack);

        serialize_bits(stream, iFightingStyle, 8);  // TODO

        serialize_object(stream, keySnapshot);

        serialize_uint8(stream, areaCode);

        bool hasBreath = breathPercent < 100;
        serialize_bool(stream, hasBreath);
        if (hasBreath)
            serialize_int(stream, breathPercent, 0, 99);
        else
            breathPercent = 100;

        return true;
    }
};

class KeyPressed : public Packet
{
    DEFINE_PACKET_TYPE(KeyPressed, ePacketType::PLAYER_KEY_SYNC, ePacketChannel::SYNC);

public:
    SenderPlayerId senderPlayerid{};
    SKeySnapshot keySnapshot;

    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed aimingRotation{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, keySnapshot);

        serialize_object(stream, currentRotation);
        serialize_object(stream, aimingRotation);

        return true;
    }
};

class PlayerBulletShot : public Packet
{
    DEFINE_PACKET_TYPE(PlayerBulletShot, ePacketType::PLAYER_BULLET_SHOT, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    eWeaponType iWeaponType;
    WorldPositionCompressed startPos{};
    WorldPositionCompressed endPos{};
    bool bHitSomething = false;
    bool bHitNetworkEntity = false;
    CNetworkEntitySerializer hitEntity{};
    ePedPieceTypes pedPiece = PED_COL_SPHERE_LEG;
    int incrementalHit = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, (int&)iWeaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, startPos);
        serialize_object(stream, endPos);

        serialize_bool(stream, bHitSomething);
        if (bHitSomething)
        {
            serialize_bool(stream, bHitNetworkEntity);
            if (bHitNetworkEntity)
            {
                serialize_object(stream, hitEntity);
                if (hitEntity.entityType == NETWORK_ENTITY_TYPE_PLAYER ||
                    hitEntity.entityType == NETWORK_ENTITY_TYPE_PED)
                {
                    serialize_int(stream, (int&)pedPiece, PED_COL_SPHERE_LEG, PED_SPHERE_HEAD);
                }
            }
        }

        if (iWeaponType == WEAPON_SHOTGUN || iWeaponType == WEAPON_SAWNOFF || iWeaponType == WEAPON_SPAS12)
        {
            serialize_int(stream, incrementalHit, -128, 127);  // TODO: find proper limits
        }
        else
        {
            incrementalHit = 0;
        }

        return true;
    }
};

class PlayerCameraSync : public Packet
{
    DEFINE_PACKET_TYPE(PlayerCameraSync, ePacketType::PLAYER_CAMERA_SYNC, ePacketChannel::SYNC);

public:
    SenderPlayerId senderPlayerid{};
    uint16_t cameraMode = MODE_NONE;
    float cameraFov = 0.0f;
    NormalizedVector front{};
    WorldPositionCompressed source{};
    NormalizedVector up{};
    float lookPitch = 0.0f;
    RadianAngleCompressed orientation{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);

        serialize_int(stream, cameraMode, MODE_NONE, MODE_AIMWEAPON_ATTACHED);

        if (Stream::IsWriting)
        {
            cameraFov = VCLAMP(cameraFov, 0.0f, 255.0f);
        }
        serialize_compressed_float(stream, cameraFov, 0.0f, 255.0f, 1.0f);

        serialize_object(stream, front);
        serialize_object(stream, source);
        serialize_object(stream, up);

        if (Stream::IsWriting)
        {
            lookPitch = NormalizeRadianAngle(lookPitch);
        }
        serialize_compressed_float(stream, lookPitch, -PI, PI, 0.1f);

        if (Stream::IsWriting)
        {
            // dont do anything, orientation will be calculated based on the front vec on reading
        }
        if (Stream::IsReading)
        {
            orientation = atan2f(front.x, front.y);
        }

        return true;
    }

    bool operator==(const PlayerCameraSync& rhs) const
    {
        return cameraMode == rhs.cameraMode && cameraFov == rhs.cameraFov && front == rhs.front &&
               source == rhs.source && up == rhs.up && lookPitch == rhs.lookPitch && orientation == rhs.orientation;
    }

    bool operator!=(const PlayerCameraSync& rhs) const { return !(*this == rhs); }
};

class SetPlayerTask : public Packet
{
    DEFINE_PACKET_TYPE(SetPlayerTask, ePacketType::SET_PLAYER_TASK, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    int taskType = TASK_NONE;
    WorldPositionCompressed vecPos{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed aimingRotation{};
    bool toggle = false;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);

        serialize_int(stream, taskType, TASK_SIMPLE_PLAYER_ON_FOOT, MAX_NUM_TASK_TYPES - 1);

        serialize_object(stream, vecPos);
        serialize_object(stream, currentRotation);
        serialize_object(stream, aimingRotation);
        serialize_bool(stream, toggle);
        return true;
    }
};

class EnExTransition : public Packet
{
    DEFINE_PACKET_TYPE(EnExTransition, ePacketType::ENEX_TRANSITION, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    WorldPositionCompressed position{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed aimingRotation{};
    uint8_t playerAreaId = AREA_MAIN_MAP;
    bool bFinished = false;
    uint8_t enexAreaId = AREA_MAIN_MAP;
    int16_t rectLeft = 0;
    int16_t rectBottom = 0;
    bool bUsesDoor = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, position);
        serialize_object(stream, currentRotation);
        serialize_object(stream, aimingRotation);
        serialize_int(stream, playerAreaId, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        serialize_bool(stream, bFinished);
        if (!bFinished)
        {
            serialize_int(stream, enexAreaId, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
            serialize_int(stream, rectLeft, -3000, 3000);
            serialize_int(stream, rectBottom, -3000, 3000);
            serialize_bool(stream, bUsesDoor);
        }
        return true;
    }
};

class PlayerPlaceWaypoint : public Packet
{
    DEFINE_PACKET_TYPE(PlayerPlaceWaypoint, ePacketType::PLAYER_PLACE_WAYPOINT, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    bool place = false;
    CVector2D position = CVector2D(0.0f, 0.0f);

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);

        serialize_bool(stream, place);
        if (place)
        {
            if (Stream::IsWriting)
            {
                position.x = std::clamp(position.x, -3000.0f, 3000.0f);
                position.y = std::clamp(position.y, -3000.0f, 3000.0f);
            }
            serialize_compressed_float(stream, position.x, -3000.0f, 3000.0f, 1.0f);
            serialize_compressed_float(stream, position.y, -3000.0f, 3000.0f, 1.0f);
        }
        return true;
    }
};

class RespawnPlayer : public Packet
{
    DEFINE_PACKET_TYPE(RespawnPlayer, ePacketType::RESPAWN_PLAYER, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    // where the game actually respawned the player (hospital, police station...); sending it avoids creating the
    // remote ped at the last on-foot position, which could be the place where he died (possibly another interior)
    WorldPositionCompressed vecPos{};
    uint8_t areaCode = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, vecPos);
        serialize_uint8(stream, areaCode);
        return true;
    }
};

// a player died or was arrested: the others play the death/arrest on the remote ped and let go of the vehicle
// seat, so the driver/passenger updates still in flight do not warp the corpse back into the car
class PlayerDeath : public Packet
{
    DEFINE_PACKET_TYPE(PlayerDeath, ePacketType::PLAYER_DEATH, ePacketChannel::EVENT);

public:
    enum eDeathState : uint8_t
    {
        DIED = 0,
        BUSTED = 1,
    };

    SenderPlayerId senderPlayerid{};
    WorldPositionCompressed pos{};
    RadianAngleCompressed heading{};
    eWeaponType weaponType = WEAPON_UNARMED;  // means of death (picks the death animation)
    uint8_t deathState = DIED;
    int8_t seatid = -1;  // seat of the vehicle he died in (-1: on foot)

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, pos);
        serialize_object(stream, heading);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_uint8(stream, deathState);
        serialize_int(stream, seatid, -1, 7);
        return true;
    }
};

class AddProjectile : public Packet
{
    DEFINE_PACKET_TYPE(AddProjectile, ePacketType::ADD_PROJECTILE, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer creator{};
    eWeaponType projectileType{};
    WorldPositionCompressed origin{};
    float force{};
    bool bDir = false;
    NormalizedVector dir{};
    bool bTarget = false;
    CNetworkEntitySerializer target{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, creator);
        serialize_int(stream, (int&)projectileType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, origin);

        if (Stream::IsWriting)
        {
            force = std::clamp(force, 0.0f, 1.0f);
        }
        serialize_compressed_float(stream, force, 0.0f, 1.0f, 0.0001f);

        serialize_bool(stream, bDir);
        if (bDir)
        {
            serialize_object(stream, dir);
        }

        serialize_bool(stream, bTarget);
        if (bTarget)
        {
            serialize_object(stream, target);
        }
        return true;
    }
};

class PlayerStats : public Packet
{
    DEFINE_PACKET_TYPE(PlayerStats, ePacketType::PLAYER_STATS, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    float stats[14]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        for (size_t i = 0; i < ARRAY_SIZE(stats); i++)
        {
            serialize_float(stream, stats[i]);
        }

        return true;
    }
};

class RebuildPlayer : public Packet
{
    DEFINE_PACKET_TYPE(RebuildPlayer, ePacketType::REBUILD_PLAYER, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    CPedClothesDesc clothesDesc{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, clothesDesc);

        return true;
    }
};

// ammo of one weapon given to another player (or sent back to the giver when it can't be accepted)
class GiveAmmo : public Packet
{
    DEFINE_PACKET_TYPE(GiveAmmo, ePacketType::GIVE_AMMO, ePacketChannel::EVENT);

public:
    playerid_t targetPlayerid{};
    SenderPlayerId senderPlayerid{};
    uint8_t weaponType{};
    int32_t ammo{};
    bool refund{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, targetPlayerid);
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, weaponType, 0, 46);
        serialize_int(stream, ammo, 0, 99999);
        serialize_bool(stream, refund);
        return true;
    }
};

// state of a player kept by the server between sessions (key: nickname)
// memcpy-able, the server writes it to disk as is (bump PERSIST_VERSION when the layout changes)
struct SPersistData
{
    static constexpr uint32_t PERSIST_VERSION = 1;

    int32_t money = 0;
    float health = 100.0f;
    float armour = 0.0f;
    bool hasPosition = false;
    float position[3]{};
    float heading = 0.0f;
    uint8_t fightingStyle = 4;
    uint8_t activeWeaponSlot = 0;
    uint8_t weaponTypes[13]{};
    uint32_t weaponAmmo[13]{};
    float floatStats[83]{};
    int32_t intStats[224]{};
    CPedClothesDesc clothes{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint32(stream, money);
        serialize_float(stream, health);
        serialize_float(stream, armour);
        serialize_bool(stream, hasPosition);
        if (hasPosition)
        {
            serialize_float(stream, position[0]);
            serialize_float(stream, position[1]);
            serialize_float(stream, position[2]);
            serialize_float(stream, heading);
        }
        serialize_uint8(stream, fightingStyle);
        serialize_int(stream, activeWeaponSlot, 0, 12);
        for (size_t i = 0; i < ARRAY_SIZE(weaponTypes); i++)
        {
            serialize_int(stream, weaponTypes[i], 0, 46);
            serialize_uint32(stream, weaponAmmo[i]);
        }
        for (size_t i = 0; i < ARRAY_SIZE(floatStats); i++)
        {
            serialize_float(stream, floatStats[i]);
        }
        for (size_t i = 0; i < ARRAY_SIZE(intStats); i++)
        {
            serialize_uint32(stream, intStats[i]);
        }
        serialize_object(stream, clothes);
        return true;
    }
};

// client -> server: current state to keep (every few seconds)
// server -> client: once after the handshake, whether the server keeps states and the saved one if any
class PlayerPersistState : public Packet
{
    DEFINE_PACKET_TYPE(PlayerPersistState, ePacketType::PLAYER_PERSIST_STATE, ePacketChannel::EVENT);

public:
    bool enabled = false;
    bool hasData = false;
    SPersistData data{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, enabled);
        serialize_bool(stream, hasData);
        if (hasData)
        {
            serialize_object(stream, data);
        }
        return true;
    }
};

// client -> server: the chat command "/guardar" asks the server to force the persistence save of everyone now
class SaveAllRequest : public Packet
{
    DEFINE_PACKET_TYPE(SaveAllRequest, ePacketType::SAVE_ALL_REQUEST, ePacketChannel::EVENT);

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        return true;
    }
};

// server -> client: send your current persistence state right now (the server is forcing a save)
class PersistSaveRequest : public Packet
{
    DEFINE_PACKET_TYPE(PersistSaveRequest, ePacketType::PERSIST_SAVE_REQUEST, ePacketChannel::EVENT);

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        return true;
    }
};

// host -> server: a piece of the savegame the host just saved (the story progress shared with everyone)
class StorySaveChunk : public Packet
{
    DEFINE_PACKET_TYPE(StorySaveChunk, ePacketType::STORY_SAVE_CHUNK, ePacketChannel::EVENT);

public:
    static constexpr uint16_t MAX_CHUNK_SIZE = 8000;

    uint32_t totalSize = 0;
    uint32_t offset = 0;
    uint16_t length = 0;
    uint8_t data[MAX_CHUNK_SIZE]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint32(stream, totalSize);
        serialize_uint32(stream, offset);
        serialize_int(stream, length, 0, MAX_CHUNK_SIZE);
        serialize_bytes(stream, data, length);
        return true;
    }
};

// the skydive/parachute animation the player's PLCHUTE script is playing (0 = not skydiving), sent on change and
// repeated every second while skydiving; the others play it on the remote ped and show the open parachute
class PlayerParachuteState : public Packet
{
    DEFINE_PACKET_TYPE(PlayerParachuteState, ePacketType::PLAYER_PARACHUTE_STATE, ePacketChannel::EVENT);

public:
    static constexpr uint8_t MAX_ANIM = 15;

    SenderPlayerId senderPlayerid{};
    uint8_t anim = 0;  // index into the parachute anim table (CParachuteSync.cpp)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, anim, 0, MAX_ANIM);
        return true;
    }
};

// test mode chat commands (/traer, /caminar) aimed at another player; the server relays it only with
// `test-commands = 1` in server-config.ini, the target applies it only with its own test mode on
class TestCommand : public Packet
{
    DEFINE_PACKET_TYPE(TestCommand, ePacketType::TEST_COMMAND, ePacketChannel::EVENT);

public:
    enum eCommand : uint8_t
    {
        TELEPORT = 0,    // go to pos/heading/interior (sent by /traer)
        WALK_START = 1,  // walk by yourself towards the sender until ~3 m (sent by /caminar)
        WALK_STOP = 2,   // stop that walk (second /caminar)
        COMMAND_MAX = WALK_STOP
    };

    playerid_t targetPlayerid{};
    SenderPlayerId senderPlayerid{};
    uint8_t command = TELEPORT;
    WorldPositionCompressed pos{};
    RadianAngleCompressed heading{};
    uint8_t interior = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, targetPlayerid);
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, command, 0, COMMAND_MAX);
        serialize_object(stream, pos);
        serialize_object(stream, heading);
        serialize_bits(stream, interior, 8);
        return true;
    }
};

// player-anim-mirror: the "action" animations the CJ of a player is playing in his game (named IFP ones of scripts and
// a whitelist of assoc groups); the copy of that player plays the same. Locomotion and weapons come from the controls.
class PlayerAnimMirror : public Packet
{
    DEFINE_PACKET_TYPE(PlayerAnimMirror, ePacketType::PLAYER_ANIM_MIRROR, ePacketChannel::SYNC);

public:
    static constexpr int MAX_ANIMS = 4;

    struct SAnim
    {
        bool named = false;          // IFP block animation (no assoc group): block + hash, else group + animId
        char block[16] = {'\0'};  // CAnimBlock::szName (named only)
        uint32_t hash = 0;           // CAnimBlendHierarchy::m_hashKey (named only)
        uint16_t group = 0;
        uint16_t animId = 0;         // global id (eAnimations.h)
        uint8_t blend = 0;           // 0..255
        uint8_t time = 0;            // fraction of the total time, 0..255
        uint8_t speed = 0;           // speed * 50
        bool playing = false;
        bool looped = false;
        bool partial = false;
    };

    SenderPlayerId senderPlayerid{};
    uint8_t count = 0;
    SAnim anims[MAX_ANIMS]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_int(stream, count, 0, MAX_ANIMS);
        for (int i = 0; i < count && i < MAX_ANIMS; i++)
        {
            SAnim& a = anims[i];
            serialize_bool(stream, a.named);
            if (a.named)
            {
                serialize_string(stream, a.block, 16);
                a.block[15] = '\0';
                serialize_uint32(stream, a.hash);
            }
            else
            {
                serialize_int(stream, a.group, 0, 4095);
                serialize_int(stream, a.animId, 0, 8191);
            }
            serialize_int(stream, a.blend, 0, 255);
            serialize_int(stream, a.time, 0, 255);
            serialize_int(stream, a.speed, 0, 255);
            serialize_bool(stream, a.playing);
            serialize_bool(stream, a.looped);
            serialize_bool(stream, a.partial);
        }
        return true;
    }
};

// player-list-coopsa: the money of a player, shown in the others' TAB list (each one keeps his own money)
class PlayerMoney : public Packet
{
    DEFINE_PACKET_TYPE(PlayerMoney, ePacketType::PLAYER_MONEY, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    int32_t money = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_uint32(stream, money);
        return true;
    }
};

}  // namespace Packets::Players

// NOLINTEND(readability-isolate-declaration)
// NOLINTEND(readability-simplify-boolean-expr)
