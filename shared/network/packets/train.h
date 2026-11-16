#pragma once
#include "../packet.h"
#include "../ids.h"

// Track-mirror train sync (TANDA_2026-11-10_glm_sync_trenes; design ported from project_gta_sa_coop
// docs/design/trains.md, GPL-3.0, option A). The host simulates every train; the other games build a real copy
// with the game's own CTrain::CreateMissionTrain (same configuration, track, node) and drive the copy's engine by
// rail distance (TRAIN_STATE), so the game code itself places every carriage. Carriages are ordinary network
// vehicle records used for identity only (seats, blips, shots, peds inside): this packet set carries just the
// chain-head state and the descriptor that binds the copies.

namespace Packets::Trains
{
// the six `train` entries of data\maps\vehicles.ide (tram 449, freight 537, streak 538, freiflat 569, streakc 570,
// freibox 590). The server has no model info, so every carriage rule keys on this shared list.
inline bool IsTrainModel(int modelid)
{
    return modelid == 449 || modelid == 537 || modelid == 538 || modelid == 569 || modelid == 570 || modelid == 590;
}

// bits of the flags fields ( TrainState::flags and STrainSpawnDesc::flags )
enum eTrainStateFlags
{
    TRAINFLAG_CLOCKWISE = 1 << 0,
    TRAINFLAG_DERAILED = 1 << 1,
    TRAINFLAG_ENGINE_HAS_NPC_DRIVER = 1 << 2,
    TRAINFLAG_STOPPED_AT_STATION = 1 << 3,
};

struct STrainCarriageDesc
{
    vehicleid_t vehicleid{};
    uint16_t modelid = 0;
    uint8_t lock = 0;  // eDoorLock as sent by VEHICLE_SPAWN-like updates, 0 = unlocked

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_uint16(stream, modelid);
        serialize_uint8(stream, lock);
        return true;
    }
};

// one train: what a receiver needs to build its copy with CreateMissionTrain, plus the engine state at the time
// of sending (so the copy starts where the host's is, not at the node)
struct STrainSpawnDesc
{
    static constexpr int MAX_CARRIAGES = 8;  // biggest vanilla configuration has 6 (freight + 5 flatbeds)

    uint8_t trainid = 0;  // host-local id, 0..31
    uint8_t track = 0;    // 0..3
    uint8_t config = 0;   // train configuration index, 0..15 (table at 0x8D44F8)
    uint8_t flags = 0;    // eTrainStateFlags
    uint16_t node = 0;    // engine start node index on the track
    uint8_t count = 0;    // carriage count
    STrainCarriageDesc carriages[MAX_CARRIAGES]{};

    float railDistance = 0.0f;  // metres along the track
    int16_t speed = 0;          // m per time step (1/50 s) x 10000, signed along the track
    uint8_t jumpCounter = 0;    // teleports / direction changes bump it: the copy snaps
    uint8_t gas = 0;
    uint8_t brake = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, trainid);
        serialize_uint8(stream, track);
        serialize_uint8(stream, config);
        serialize_uint8(stream, flags);
        serialize_uint16(stream, node);
        serialize_uint8(stream, count);
        for (int i = 0; i < count && i < MAX_CARRIAGES; i++)
        {
            if (!carriages[i].Serialize(stream))
                return false;
        }
        serialize_float(stream, railDistance);
        serialize_int(stream, speed, -10000, 10000);
        serialize_uint8(stream, jumpCounter);
        serialize_uint8(stream, gas);
        serialize_uint8(stream, brake);
        return true;
    }
};

class TrainState : public Packet
{
    DEFINE_PACKET_TYPE(TrainState, ePacketType::TRAIN_STATE, ePacketChannel::SYNC);

public:
    uint8_t trainid = 0;
    uint8_t seq = 0;  // drops stale out-of-order states (u8 wrap-around aware)
    uint8_t track = 0;
    uint8_t flags = 0;  // eTrainStateFlags
    uint8_t jumpCounter = 0;
    float railDistance = 0.0f;
    int16_t speed = 0;
    uint8_t gas = 0;
    uint8_t brake = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, trainid);
        serialize_uint8(stream, seq);
        serialize_uint8(stream, track);
        serialize_uint8(stream, flags);
        serialize_uint8(stream, jumpCounter);
        serialize_float(stream, railDistance);
        serialize_int(stream, speed, -10000, 10000);
        serialize_uint8(stream, gas);
        serialize_uint8(stream, brake);
        return true;
    }
};

class TrainEvent : public Packet
{
    DEFINE_PACKET_TYPE(TrainEvent, ePacketType::TRAIN_EVENT, ePacketChannel::EVENT);

public:
    enum eKind : uint8_t
    {
        KIND_SPAWN = 0,     // count = 1: build the copy from the descriptor (records already exist via VEHICLE_SPAWN)
        KIND_REMOVE = 1,    // the host's train is gone: remove the whole copy (trainid)
        KIND_SNAPSHOT = 2,  // everything the host has: for a client that just connected or restarted
    };

    uint8_t kind = KIND_SPAWN;
    uint8_t trainid = 0;  // KIND_REMOVE only
    bool reset = false;   // KIND_SNAPSHOT: forget every train not listed in it
    uint8_t count = 0;
    STrainSpawnDesc trains[8]{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, kind);
        serialize_uint8(stream, trainid);
        serialize_bool(stream, reset);
        serialize_uint8(stream, count);
        for (int i = 0; i < count && i < 8; i++)
        {
            if (!trains[i].Serialize(stream))
                return false;
        }
        return true;
    }
};
}  // namespace Packets::Trains
