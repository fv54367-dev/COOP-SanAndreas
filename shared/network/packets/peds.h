#pragma once
#include <eModelID.h>
#include <ePedType.h>
#include <CPed.h>
#include <CVehicle.h>
#include <eGlobalSpeechContexts.h>

namespace Packets::Peds
{
class PedSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PedSpawn, ePacketType::PED_SPAWN, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    eModelID modelId = MODEL_MALE01;
    ePedType pedType = PED_TYPE_CIVMALE;
    WorldPositionCompressed pos{};
    eCharCreatedBy createdBy = MISSION_CHAR;
    bool interiorPopulation = false;
    uint8_t areaCode = 0;
    char specialModelName[8] = {'\0'};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_int(stream, (int&)modelId, MODEL_NULL, MODEL_SPECIAL10);
        serialize_int(stream, (int&)pedType, PED_TYPE_PLAYER1, PED_TYPE_MISSION8);
        serialize_object(stream, pos);
        serialize_int(stream, (int&)createdBy, UNUSED_CHAR, REPLAY_CHAR);
        serialize_bool(stream, interiorPopulation);
        serialize_uint8(stream, areaCode);
        serialize_string(stream, specialModelName, 8);
        specialModelName[ARRAY_SIZE(specialModelName) - 1] = '\0';
        return true;
    }
};

// Request: area/position only. Response: the server fills the elected owner.
class InteriorPopulation : public Packet
{
    DEFINE_PACKET_TYPE(InteriorPopulation, ePacketType::INTERIOR_POPULATION, ePacketChannel::EVENT);
public:
    uint8_t area = 0;
    WorldPositionCompressed pos{};
    playerid_t owner{};
private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, area);
        serialize_object(stream, pos);
        serialize_playerid(stream, owner);
        return true;
    }
};

class PedRemove : public Packet
{
    DEFINE_PACKET_TYPE(PedRemove, ePacketType::PED_REMOVE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class AssignPedSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignPedSyncer, ePacketType::ASSIGN_PED, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool syncing = true;  // the receiver syncs the ped from now on (false: it stops)
    // B1 handover: the previous owner let the ped go, create it at pos if you don't have it
    bool takeover = false;
    WorldPositionCompressed pos{};
    float heading = 0.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_bool(stream, syncing);
        serialize_bool(stream, takeover);
        serialize_object(stream, pos);
        serialize_float(stream, heading);
        return true;
    }
};

class PedOnFoot : public Packet
{
    DEFINE_PACKET_TYPE(PedOnFoot, ePacketType::PED_ONFOOT, ePacketChannel::SYNC);

public:
    pedid_t pedid{};
    WorldPositionCompressed pos{};
    MoveSpeedCompressed velocity{};
    bool hasVelocity = false;  // npc-velocity-sync: velocity travels only when set (off = old wire format + 1 bit)
    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};
    RadianAngleCompressed aimingRotation{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed lookDirection{};  // this one isnt needed i think
    eMoveState moveState = PEDMOVE_STILL;
    bool bDucked = false;
    bool bAiming = false;
    uint8_t fightingStyle = 4;
    WorldPositionCompressed weaponAim{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_object(stream, pos);
        serialize_bool(stream, hasVelocity);
        if (hasVelocity)
        {
            serialize_object(stream, velocity);
        }
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_object(stream, aimingRotation);
        serialize_object(stream, currentRotation);
        serialize_object(stream, lookDirection);
        serialize_int(stream, (int&)moveState, PEDMOVE_NONE, PEDMOVE_SPRINT);
        serialize_bool(stream, bDucked);
        serialize_bool(stream, bAiming);
        serialize_uint8(stream, fightingStyle);  // todo
        if (bAiming)
        {
            serialize_object(stream, weaponAim);
        }
        return true;
    }
};

// root task of a hosted ped plus the animation we want replicated on the copies (npc-task-sync)
class PedTaskState : public Packet
{
    DEFINE_PACKET_TYPE(PedTaskState, ePacketType::PED_ADD_TASK, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    uint16_t taskType = 0;  // eTaskType of the root primary task (TASK_NONE when it has none)
    bool hasVehicle = false;  // enter / leave car tasks
    vehicleid_t vehicleid{};
    int8_t seat = -1;  // passenger seat (0..7), -1 = driver or not used
    int8_t door = 0;   // target door (0, 8, 9, 11)
    bool hasAnim = false;
    uint16_t animGroup = 0;  // eAnimGroup of the association, see IsRelevantAnimation
    uint16_t animId = 0;     // global eAnimations id, as CAnimManager::BlendAnimation expects
    uint8_t serial = 0;      // last state wins, an older packet is dropped by the receiver
    // D npc-named-anim-sync: an animation with a name (an IFP block that is not one of the whitelisted assoc
    // groups: dancing, bar, strip, casino, food...). The receiver loads the block by name and blends the
    // hierarchy itself; serialized only when namedAnim (with the switch off the wire format is the old one)
    bool namedAnim = false;
    char animBlock[16] = {'\0'};  // CAnimBlock::szName the hierarchy lives in
    uint32_t animHash = 0;        // CAnimBlendHierarchy::m_hashKey
    uint16_t animFlags = 0;       // CAnimBlendAssociation::m_nFlags (loop / freeze / translate...)
    float animTime = 0.0f;        // CAnimBlendAssociation::m_fCurrentTime
    float animSpeed = 1.0f;       // CAnimBlendAssociation::m_fSpeed

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_uint16(stream, taskType);
        serialize_bool(stream, hasVehicle);
        if (hasVehicle)
        {
            serialize_vehicleid(stream, vehicleid);
            serialize_int(stream, seat, -1, 7);
            serialize_int(stream, door, 0, 11);
        }
        serialize_bool(stream, hasAnim);
        if (hasAnim)
        {
            serialize_uint16(stream, animGroup);
            serialize_uint16(stream, animId);
        }
        serialize_uint8(stream, serial);
        serialize_bool(stream, namedAnim);
        if (namedAnim)
        {
            serialize_string(stream, animBlock, 16);
            animBlock[15] = '\0';
            serialize_uint32(stream, animHash);
            serialize_uint16(stream, animFlags);
            serialize_float(stream, animTime);
            serialize_float(stream, animSpeed);
        }
        return true;
    }
};

class PedDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(PedDriverUpdate, ePacketType::PED_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    pedid_t pedid{};
    vehicleid_t vehicleid{};
    eVehicleType vehicleSubType = VEHICLE_AUTOMOBILE;

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    Packets::Players::SHealthSnapshot pedHealth{};
    Packets::Players::SWeaponSnapshot pedWeapon{};

    float health{};

    eDoorLock locked = DOORLOCK_UNLOCKED;
    float gasPedal = 0.0f;
    float breakPedal = 0.0f;
    float steerAngle = 0.0f;

    bool horn{};
    bool siren{};

    // subType specific fields
    float bikeLean{};               // bike/bmx
    float controlPedaling{};        // bmx
    float planeGearState{};         // plane
    uint16_t miscComponentAngle{};  // automobile/mtruck/plane

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
#pragma region IDs
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, (int&)vehicleSubType, VEHICLE_AUTOMOBILE, VEHICLE_TRAILER);
#pragma endregion
#pragma region clamp stuff
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
            health = std::clamp(health, 0.0f, 4000.0f);
            gasPedal = std::clamp(gasPedal, -1.0f, 1.0f);
            breakPedal = std::clamp(breakPedal, -1.0f, 1.0f);
            steerAngle = std::clamp(steerAngle, -1.0f, 1.0f);
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
            controlPedaling = std::clamp(controlPedaling, -5.0f, 5.0f);
        }
#pragma endregion
#pragma region matrix speeds
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);

        bool sendTurnSpeed = false;
        if (Stream::IsWriting)
        {
            if (std::abs(turnSpeed.x) >= 0.0001f || std::abs(turnSpeed.y) >= 0.0001f ||
                std::abs(turnSpeed.z) >= 0.0001f)
            {
                sendTurnSpeed = true;
            }
        }
        serialize_bool(stream, sendTurnSpeed);
        if (sendTurnSpeed)
        {
            serialize_compressed_float(stream, turnSpeed.x, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.y, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.z, -0.5f, 0.5f, 0.0001f);
        }

#pragma endregion
#pragma region painjob
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }
        serialize_int(stream, paintjob, -1, 2);

#pragma endregion
#pragma region ped health
        {
            serialize_object(stream, pedHealth);
            serialize_object(stream, pedWeapon);
        }
#pragma endregion
#pragma region health doors
        serialize_compressed_float(stream, health, 0.0f, 4000.0f, 1.0f);

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);
#pragma endregion
#pragma region pedals
        {
            serialize_compressed_float(stream, gasPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, breakPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, steerAngle, -1.0f, 1.0f, 0.01f);
        }
        serialize_bool(stream, horn);
        serialize_bool(stream, siren);
#pragma endregion
#pragma region type specific

        if (vehicleSubType == VEHICLE_BIKE || vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, controlPedaling, -5.0f, 5.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_PLANE || vehicleSubType == VEHICLE_HELI)
        {
            // a plane's gear, or a heli's rotor; a second bit for the heli's searchlight (planeGearState 2 / 3, 0.4.52)
            if (Stream::IsWriting)
            {
                bool temp = planeGearState > 0.0f;
                bool light = planeGearState >= 2.0f;
                serialize_bool(stream, temp);
                serialize_bool(stream, light);
            }
            else if (Stream::IsReading)
            {
                bool temp, light;
                serialize_bool(stream, temp);
                serialize_bool(stream, light);
                planeGearState = (temp ? 1.0f : 0.0f) + (light ? 2.0f : 0.0f);
            }
        }
        if (vehicleSubType == VEHICLE_AUTOMOBILE || vehicleSubType == VEHICLE_MTRUCK || vehicleSubType == VEHICLE_PLANE)
        {
            bool syncAngle = false;
            if (Stream::IsWriting && miscComponentAngle != 0)
            {
                syncAngle = true;
            }
            serialize_bool(stream, syncAngle);
            if (syncAngle)
            {
                serialize_uint16(stream, miscComponentAngle);
            }
        }
#pragma endregion

        return true;
    }
};

class PedPassengerSync : public Packet
{
    DEFINE_PACKET_TYPE(PedPassengerSync, ePacketType::PED_PASSENGER_UPDATE, ePacketChannel::SYNC);

public:
    pedid_t pedid{};
    vehicleid_t vehicleid{};

    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};

    int8_t seatid;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits
        return true;
    }
};

class PedShotSync : public Packet
{
    DEFINE_PACKET_TYPE(PedShotSync, ePacketType::PED_SHOT_SYNC, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    eWeaponType weaponType = WEAPON_UNARMED;
    WorldPositionCompressed origin{};
    WorldPositionCompressed effect{};
    WorldPositionCompressed target{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, origin);
        serialize_object(stream, effect);
        serialize_object(stream, target);
        return true;
    }
};

class PedSay : public Packet
{
    DEFINE_PACKET_TYPE(PedSay, ePacketType::PED_SAY, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer entity{};
    eGlobalSpeechContexts phraseId = CONTEXT_GLOBAL_NO_SPEECH;
    uint32_t startTimeDelay = 0;
    bool overrideSilence = false;
    bool isForceAudible = false;
    bool isFrontEnd = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, entity);
        if (entity.entityType != NETWORK_ENTITY_TYPE_PLAYER && entity.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_int(stream, (int&)phraseId, CONTEXT_GLOBAL_NO_SPEECH + 1, CONTEXT_GLOBAL_END - 1);
        serialize_uint32(stream, startTimeDelay);
        serialize_bool(stream, overrideSilence);
        serialize_bool(stream, isForceAudible);
        serialize_bool(stream, isFrontEnd);
        return true;
    }
};

class PedClaimOnRelease : public Packet
{
    DEFINE_PACKET_TYPE(PedClaimOnRelease, ePacketType::PED_CLAIM_ON_RELEASE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedCancelClaim : public Packet
{
    DEFINE_PACKET_TYPE(PedCancelClaim, ePacketType::PED_CANCEL_CLAIM, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedResetAllClaims : public Packet
{
    DEFINE_PACKET_TYPE(PedResetAllClaims, ePacketType::PED_RESET_ALL_CLAIMS, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedTakeHost : public Packet
{
    DEFINE_PACKET_TYPE(PedTakeHost, ePacketType::PED_TAKE_HOST, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool allowReturnToPreviousHost = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_bool(stream, allowReturnToPreviousHost);
        return true;
    }
};

// a player pulled an NPC out of a car: its syncer must release it instead of warping it back in
class PedJacked : public Packet
{
    DEFINE_PACKET_TYPE(PedJacked, ePacketType::PED_JACKED, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    vehicleid_t vehicleid{};
    int8_t seatid = -1;         // -1 = driver, 0..7 = passenger
    int8_t door = 0;            // target door (0, 8, 9, 11)
    playerid_t thiefPlayerId{};  // the player that pulled it out (the ped must react to it)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);
        serialize_int(stream, door, 0, 11);
        serialize_playerid(stream, thiefPlayerId);
        return true;
    }
};

// B1: the syncer took the ped out of its world (streamed out) and offers it to someone else; the server
// decides who hosts it (or removes it). `reason`: 0 = culled (far from the sender), 1 = the assignee rejected it.
class PedRelease : public Packet
{
    DEFINE_PACKET_TYPE(PedRelease, ePacketType::PED_RELEASE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    WorldPositionCompressed pos{};
    float heading = 0.0f;
    float health = 0.0f;
    uint8_t areaCode = 0;
    uint8_t reason = 0;  // 0 = culled, 1 = rejected

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_object(stream, pos);
        serialize_float(stream, heading);
        serialize_float(stream, health);
        serialize_uint8(stream, areaCode);
        serialize_uint8(stream, reason);
        return true;
    }
};

// Paramedic coop: the script (on the mission owner) gives the patient a command that must run where the patient is
// simulated. ENTER: hand the patient to the player driving the ambulance and make it board it there; GOTO / EXPLODE /
// RELEASE: run that command on the patient's current syncer (see CPatientHandover).
class PedPatientTask : public Packet
{
    DEFINE_PACKET_TYPE(PedPatientTask, ePacketType::PED_PATIENT_TASK, ePacketChannel::EVENT);

public:
    enum eAction : uint8_t
    {
        ENTER = 0,    // board vehicleid as passenger (seat -2: any)
        GOTO = 1,     // run to pos (leaves the vehicle first)
        EXPLODE = 2,  // time out: explode head and remove elegantly
        RELEASE = 3,  // delivered: keep its task and mark it as no longer needed
    };

    pedid_t pedid{};
    vehicleid_t vehicleid{};
    playerid_t targetPlayerId{};  // ENTER: the player that takes the patient
    uint8_t action = ENTER;
    CVector pos{};
    uint32_t param = 0;  // ENTER: seat (as int, -2 any), GOTO: move state

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_playerid(stream, targetPlayerId);
        serialize_int(stream, action, 0, 3);
        serialize_float(stream, pos.x);
        serialize_float(stream, pos.y);
        serialize_float(stream, pos.z);
        serialize_uint32(stream, param);
        return true;
    }
};

// ped-damage-authority: a player hurt the copy of an NPC he does not sync (bullets, melee, run over). The server
// sends it to the syncer of the NPC, the only one that decides its health and its death.
class PedDamage : public Packet
{
    DEFINE_PACKET_TYPE(PedDamage, ePacketType::PED_DAMAGE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    playerid_t attackerId{};  // set by the server
    eWeaponType weaponType = WEAPON_UNARMED;
    uint8_t pedPiece = 0;
    uint16_t loss = 0;  // health + armour the copy lost
    bool killed = false;  // the copy died

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_playerid(stream, attackerId);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_int(stream, pedPiece, 0, 15);
        serialize_int(stream, loss, 0, 5000);
        serialize_bool(stream, killed);
        return true;
    }
};

// ped-damage-authority: an NPC hurt a player in the game of its syncer (bullets, melee). Sent to that player only,
// who applies it to himself: the shots replayed on the copies do not decide it.
class PedHitPlayer : public Packet
{
    DEFINE_PACKET_TYPE(PedHitPlayer, ePacketType::PED_HIT_PLAYER, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    playerid_t playerid{};
    eWeaponType weaponType = WEAPON_UNARMED;
    uint8_t pedPiece = 0;
    uint16_t loss = 0;  // health + armour the player lost in the game of the syncer

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_playerid(stream, playerid);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_int(stream, pedPiece, 0, 15);
        serialize_int(stream, loss, 0, 5000);
        return true;
    }
};

// npc-anim-mirror: the animations an NPC on foot is playing right now in the game of its syncer (the strongest ones,
// by assoc group and id). Its copies play exactly this set instead of guessing it from the move state.
class PedAnimSet : public Packet
{
    DEFINE_PACKET_TYPE(PedAnimSet, ePacketType::PED_ANIM_SET, ePacketChannel::SYNC);

public:
    static constexpr int MAX_ANIMS = 6;

    struct SAnim
    {
        uint16_t group = 0;
        uint16_t animId = 0;   // global id (eAnimations.h)
        uint8_t blend = 0;     // blend amount, 0..255
        uint8_t time = 0;      // current time as a fraction of the total time, 0..255
        uint8_t speed = 0;     // speed * 50
        bool playing = false;
    };

    pedid_t pedid{};
    uint8_t count = 0;
    SAnim anims[MAX_ANIMS]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_int(stream, count, 0, MAX_ANIMS);
        for (int i = 0; i < count && i < MAX_ANIMS; i++)
        {
            serialize_int(stream, anims[i].group, 0, 4095);
            serialize_int(stream, anims[i].animId, 0, 8191);
            serialize_int(stream, anims[i].blend, 0, 255);
            serialize_int(stream, anims[i].time, 0, 255);
            serialize_int(stream, anims[i].speed, 0, 255);
            serialize_bool(stream, anims[i].playing);
        }
        return true;
    }
};

class PedRecruit : public Packet
{
    DEFINE_PACKET_TYPE(PedRecruit, ePacketType::PED_RECRUIT, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool recruited{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_bool(stream, recruited);
        return true;
    }
};

}  // namespace Packets::Peds
