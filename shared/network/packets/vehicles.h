#pragma once
#include <CVehicle.h>

namespace Packets::Vehicles
{
class VehicleSpawn : public Packet
{
    DEFINE_PACKET_TYPE(VehicleSpawn, ePacketType::VEHICLE_SPAWN, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    uint16_t modelid{};
    WorldPositionCompressed pos{};
    RadianAngleCompressed rot{};
    uint8_t color1 = 0;
    uint8_t color2 = 0;
    eVehicleCreatedBy createdBy = RANDOM_VEHICLE;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, modelid, MODEL_LANDSTAL, MODEL_UTILTR1);
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        serialize_int(stream, (int&)createdBy, RANDOM_VEHICLE, PERMANENT_VEHICLE);
        return true;
    }
};
class VehicleRemove : public Packet
{
    DEFINE_PACKET_TYPE(VehicleRemove, ePacketType::VEHICLE_REMOVE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        return true;
    }
};

class VehicleIdleUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehicleIdleUpdate, ePacketType::VEHICLE_IDLE_UPDATE, ePacketChannel::SYNC);

public:
    vehicleid_t vehicleid{};

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    float health{};

    float planeGearState{};
    eDoorLock locked{};

    // the horn / siren (also an alarm of an empty car) and the lights of an unoccupied vehicle
    bool horn = false;
    bool siren = false;
    bool lights = false;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);   

        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
        }

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

        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);

        if (Stream::IsWriting)
        {
            health = std::clamp(health, 0.0f, 4000.0f);
        }
        serialize_compressed_float(stream, health, 0.0f, 4000.0f, 1.0f);

        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }

        serialize_int(stream, paintjob, -1, 2);

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

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);

        serialize_bool(stream, horn);
        serialize_bool(stream, siren);
        serialize_bool(stream, lights);

        return true;
    }
};

class VehicleDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehicleDriverUpdate, ePacketType::VEHICLE_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    SenderPlayerId senderPlayerid{};
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

    Packets::Players::SHealthSnapshot playerHealth{};
    Packets::Players::SWeaponSnapshot playerWeapon{};
    Packets::Players::SKeySnapshot playerKeys{};

    float health{};

    float bikeLean{};
    unsigned short miscComponentAngle{};  // hydra thrusters

    float planeGearState{};
    eDoorLock locked{};
    int8_t radioStation = -1;  // the driver's radio, the passengers tune to it (-1: unknown/off)
    vehicleid_t trailerid{};  // the trailer the vehicle tows, if any

    // horn edge / siren (patrol cars) / lights of the vehicle driven by this player
    bool horn = false;
    bool siren = false;
    bool lights = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
        }

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

        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);

        if (Stream::IsWriting)
        {
            health = std::clamp(health, 0.0f, 4000.0f);
        }
        serialize_compressed_float(stream, health, 0.0f, 4000.0f, 1.0f);

        serialize_object(stream, playerHealth);
        serialize_object(stream, playerWeapon);
        serialize_object(stream, playerKeys);

        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }

        serialize_int(stream, paintjob, -1, 2);

        if (Stream::IsWriting)
        {
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
        }
        serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);

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

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);
        serialize_int(stream, radioStation, -1, 13);
        bool hasTrailer = trailerid.valid();
        serialize_bool(stream, hasTrailer);
        if (hasTrailer)
            serialize_vehicleid(stream, trailerid);
        else if (Stream::IsReading)
            trailerid = vehicleid_t();

        serialize_bool(stream, horn);
        serialize_bool(stream, siren);
        serialize_bool(stream, lights);

        return true;
    }
};

class VehicleEnter : public Packet
{
    DEFINE_PACKET_TYPE(VehicleEnter, ePacketType::VEHICLE_ENTER, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    int8_t seatid = 0;  // 3bits
    bool bForce = false;
    bool bPassenger = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly
        serialize_bool(stream, bForce);
        serialize_bool(stream, bPassenger);

        return true;
    }
};

// the server refused a seat because another player already holds it (the sender keeps sending driver / passenger
// updates for that seat): the sender must get out instead of warping itself in again on every update
class VehicleSeatDenied : public Packet
{
    DEFINE_PACKET_TYPE(VehicleSeatDenied, ePacketType::VEHICLE_SEAT_DENIED, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    int8_t seatid = -1;  // same convention as VehicleEnter: -1 = driver, 0..7 = passenger

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);

        return true;
    }
};

class VehicleExit : public Packet
{
    DEFINE_PACKET_TYPE(VehicleExit, ePacketType::VEHICLE_EXIT, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    bool bForce = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_bool(stream, bForce);

        return true;
    }
};

// a local player started pulling a remote player out of a vehicle (the victim is the occupant of the seat)
class PlayerJacked : public Packet
{
    DEFINE_PACKET_TYPE(PlayerJacked, ePacketType::PLAYER_JACKED, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    playerid_t victimPlayerId{};
    vehicleid_t vehicleid{};
    int8_t seatid = -1;  // -1 = driver, 0..7 = passenger
    int8_t door = 0;     // target door (0, 8, 9 or 11)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_playerid(stream, victimPlayerId);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);
        serialize_int(stream, door, 0, 11);
        return true;
    }
};

// the sender is being pulled out of a vehicle by any ped (for example an NPC); the others replay the animation
class PlayerBeingJacked : public Packet
{
    DEFINE_PACKET_TYPE(PlayerBeingJacked, ePacketType::PLAYER_BEING_JACKED, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    int8_t seatid = -1;  // -1 = driver, 0..7 = passenger
    int8_t door = 0;     // target door (0, 8, 9 or 11)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);
        serialize_int(stream, door, 0, 11);
        return true;
    }
};

class VehicleDamage : public Packet
{
    DEFINE_PACKET_TYPE(VehicleDamage, ePacketType::VEHICLE_DAMAGE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    CDamageManager damageManager{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, damageManager);
        return true;
    }
};

class VehicleComponentAdd : public Packet
{
    DEFINE_PACKET_TYPE(VehicleComponentAdd, ePacketType::VEHICLE_COMPONENT_ADD, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    int componentid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, componentid, 0, 19999);

        return true;
    }
};

class VehicleComponentRemove : public Packet
{
    DEFINE_PACKET_TYPE(VehicleComponentRemove, ePacketType::VEHICLE_COMPONENT_REMOVE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    int componentid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, componentid, 0, 19999);

        return true;
    }
};

class VehiclePassengerUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehiclePassengerUpdate, ePacketType::VEHICLE_PASSENGER_UPDATE, ePacketChannel::SYNC);
   
public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    Packets::Players::SHealthSnapshot playerHealth{};
    Packets::Players::SWeaponSnapshot playerWeapon{};
    Packets::Players::SKeySnapshot playerKeys{};
    bool driveby = false;
    int8_t seatid = -1;  // -1 = the ped isn't in a passenger seat (never claim seat 0 by accident)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, playerHealth);
        serialize_object(stream, playerWeapon);
        serialize_object(stream, playerKeys);
        serialize_bool(stream, driveby);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits

        return true;
    }
};

class AssignVehicleSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignVehicleSyncer, ePacketType::ASSIGN_VEHICLE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    bool syncing = true;  // the receiver syncs the vehicle from now on (false: it stops)
    // B1 handover: the previous owner let the vehicle go, create it at pos if you don't have it
    bool takeover = false;
    WorldPositionCompressed pos{};
    float heading = 0.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_bool(stream, syncing);
        serialize_bool(stream, takeover);
        serialize_object(stream, pos);
        serialize_float(stream, heading);

        return true;
    }
};

class SetVehicleCreatedBy : public Packet
{
    DEFINE_PACKET_TYPE(SetVehicleCreatedBy, ePacketType::SET_VEHICLE_CREATED_BY, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    eVehicleCreatedBy createdBy{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, (int&)createdBy, RANDOM_VEHICLE, PERMANENT_VEHICLE);

        return true;
    }
};
class VehicleHydraulicSync : public Packet
{
    DEFINE_PACKET_TYPE(VehicleHydraulicSync, ePacketType::VEHICLE_HYDRAULIC_SYNC, ePacketChannel::SYNC);

public:
    vehicleid_t vehicleid{};
    uint16_t angle = 0;             // CAutomobile::m_wMiscComponentAngle (hydraulic phase)
    int16_t gunLeftRight = 0;       // pad right stick X, -128..128 (direction of the bounce)
    int16_t gunUpDown = 0;          // pad right stick Y, -128..128
    bool jump = false;              // the horn/hydraulic key went down (full bounce)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_uint16(stream, angle);
        serialize_int(stream, gunLeftRight, -128, 128);
        serialize_int(stream, gunUpDown, -128, 128);
        serialize_bool(stream, jump);
        return true;
    }
};
// the opening ratio of the 6 doors of a synced automobile (0 = closed, 255 = fully open), in eDoors order:
// bonnet, boot, front left, front right, rear left, rear right
class VehicleDoors : public Packet
{
    DEFINE_PACKET_TYPE(VehicleDoors, ePacketType::VEHICLE_DOORS, ePacketChannel::SYNC);

public:
    vehicleid_t vehicleid{};
    uint8_t ratio[6]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        for (auto& value : ratio)
        {
            serialize_uint8(stream, value);
        }
        return true;
    }
};

// B1: the syncer took the vehicle out of its world (streamed out) and offers it to someone else; the server
// decides who hosts it (or removes it). `reason`: 0 = culled (far from the sender), 1 = the assignee rejected it.
class VehicleRelease : public Packet
{
    DEFINE_PACKET_TYPE(VehicleRelease, ePacketType::VEHICLE_RELEASE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    WorldPositionCompressed pos{};
    uint8_t areaCode = 0;
    uint8_t reason = 0;  // 0 = culled, 1 = rejected

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, pos);
        serialize_uint8(stream, areaCode);
        serialize_uint8(stream, reason);
        return true;
    }
};
}  // namespace Packets::Vehicles
