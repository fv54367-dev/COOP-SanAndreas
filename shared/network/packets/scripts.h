#pragma once
#include <cmath>
#include <CEntryExit.h>
#include <CGame.h>

namespace Packets::Scripts
{
class OnMissionFlagSync : public Packet
{
    DEFINE_PACKET_TYPE(OnMissionFlagSync, ePacketType::ON_MISSION_FLAG_SYNC, ePacketChannel::SCRIPT);

public:
    bool bOnMission = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bOnMission);
        return true;
    }
};

// the host left in the middle of a coop mission: the others drop the mission state he left on them (mirrored
// OnAMissionFlag, checkpoints, blips, mission HUD) and show a notice; sent by the server before it picks a new host
class MissionAborted : public Packet
{
    DEFINE_PACKET_TYPE(MissionAborted, ePacketType::MISSION_ABORTED, ePacketChannel::SCRIPT);

public:
    enum eReason : uint8_t
    {
        HOST_LEFT = 0,
    };

    uint8_t reason = HOST_LEFT;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, reason);
        return true;
    }
};

struct _EnExPayload
{
    uint8_t areaId = 0;
    SEntryExitFlags flags{};
    int16_t rectLeft = 0;
    int16_t rectBottom = 0;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, areaId, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        serialize_object(stream, flags);
        serialize_int(stream, rectLeft, -3000, 3000);
        serialize_int(stream, rectBottom, -3000, 3000);
        return true;
    }
};

class EnExSync : public Packet
{
    DEFINE_PACKET_TYPE(EnExSync, ePacketType::ENEX_SYNC, ePacketChannel::EVENT);

public:
    static constexpr size_t MAX_ENEX_COUNT = 400;  // max length of the pool, got it here 0x43F927

    bool bDisabled = false;
    bool bBurglary = false;
    int count = 0;
    _EnExPayload enexes[MAX_ENEX_COUNT];

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, bDisabled);
        serialize_bool(stream, bBurglary);
        serialize_int(stream, count, 0, MAX_ENEX_COUNT);
        for (size_t i = 0; i < count; i++)
        {
            serialize_object(stream, enexes[i]);
        }
        return true;
    }
};

#ifdef COOP_SERVER
inline static EnExSync g_lastEnExData{};
inline static CNetworkPlayer* g_pLastEnExPlayerOwner = nullptr;
#endif

class AddMessageGXT : public Packet
{
    DEFINE_PACKET_TYPE(AddMessageGXT, ePacketType::ADD_MESSAGE_GXT, ePacketChannel::SCRIPT);

public:
    enum eGXTMsgType
    {
        sync_COMMAND_PRINT = 0,
        sync_COMMAND_PRINT_BIG,
        sync_COMMAND_PRINT_NOW,
        sync_COMMAND_PRINT_HELP
    };

    playerid_t forWhoPlayerId{};  // TODO(v0.3.1-alpha): dont send s2c
    eGXTMsgType type = sync_COMMAND_PRINT;
    uint32_t time = 0;
    uint8_t flag = 0;
    char gxt[8]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);
        serialize_int(stream, (int&)type, sync_COMMAND_PRINT, sync_COMMAND_PRINT_HELP);
        if (type != sync_COMMAND_PRINT_HELP)
        {
            serialize_uint32(stream, time);
            serialize_uint8(stream, flag);
        }

        serialize_string(stream, gxt, 8);
        gxt[ARRAY_SIZE(gxt) - 1] = '\0';
        return true;
    }
};

class RemoveMessageGXT : public Packet
{
    DEFINE_PACKET_TYPE(RemoveMessageGXT, ePacketType::REMOVE_MESSAGE_GXT, ePacketChannel::SCRIPT);

public:
    playerid_t forWhoPlayerId{};
    char gxt[8]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, forWhoPlayerId);

        serialize_string(stream, gxt, 8);
        gxt[ARRAY_SIZE(gxt) - 1] = '\0';
        return true;
    }
};

class StartCutscene : public Packet
{
    DEFINE_PACKET_TYPE(StartCutscene, ePacketType::START_CUTSCENE, ePacketChannel::SCRIPT);

public:
    char name[8];
    eVisibleArea currArea;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_string(stream, name, 8);
        name[ARRAY_SIZE(name) - 1] = '\0';
        serialize_int(stream, (int&)currArea, AREA_MAIN_MAP, MAX_VISIBLE_AREAS - 1);
        return true;
    }
};

class SkipCutscene : public Packet
{
    DEFINE_PACKET_TYPE(SkipCutscene, ePacketType::SKIP_CUTSCENE, ePacketChannel::SCRIPT);

public:
    SenderPlayerId senderPlayerid{};
    bool skipNow = false;  // false: the sender votes to skip, true: the host skips the scene for everyone

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_bool(stream, skipNow);
        return true;
    }
};

class PlayMissionAudio : public Packet
{
    DEFINE_PACKET_TYPE(PlayMissionAudio, ePacketType::PLAY_MISSION_AUDIO, ePacketChannel::SCRIPT);

public:
    uint8_t slotid = 0;
    int audioid = 0;
    bool preloadOnly = false;  // the host started loading it, play it later (keeps voice and subtitles together)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, slotid, 0, 3);
        serialize_uint32(stream, audioid);
        serialize_bool(stream, preloadOnly);
        return true;
    }
};

class TeleportPlayerScripted : public Packet
{
    DEFINE_PACKET_TYPE(TeleportPlayerScripted, ePacketType::TELEPORT_PLAYER_SCRIPTED, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};
    WorldPositionCompressed pos{};
    RadianAngleCompressed heading{};
    unsigned char area{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_object(stream, pos);
        serialize_object(stream, heading);
        serialize_int(stream, area, 0, 255);
        return true;
    }
};

class PutInVehicleScripted : public Packet
{
    DEFINE_PACKET_TYPE(PutInVehicleScripted, ePacketType::PUT_IN_VEHICLE_SCRIPTED, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};
    vehicleid_t vehicleid{};
    int8_t seat = -1;  // passenger seat in the host car, -1: give the player an equal car of their own, -2: park it there, -3: driver of existing vehicle
    uint16_t model = 0; // 0 with seat >= 0: warp into existing authority vehicle, no fallback.
    CVector pos{};
    float heading = 0.0f;
    uint8_t color1 = 0;
    uint8_t color2 = 0;
    bool walk = false;  // native passenger entry, without warp or fallback copy

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seat, -3, 7);
        serialize_uint16(stream, model);
        serialize_vector(stream, pos);
        serialize_float(stream, heading);
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        serialize_bool(stream, walk);
        return true;
    }
};

class OddjobReward : public Packet
{
    DEFINE_PACKET_TYPE(OddjobReward, ePacketType::ODDJOB_REWARD, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};   // target player
    uint8_t rewardId{};
    int32_t doneFlagOffset{}; // byte offset of a global var in CTheScripts::ScriptSpace, -1 = none

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_int(stream, rewardId, 0, 255);
        serialize_int(stream, doneFlagOffset, -1, 200000 - (int)sizeof(int));
        return true;
    }
};

// the host's on-screen mission HUD: clock (e.g. the Vigilante time limit) and the 4 counters/bars (e.g. Sweet's
// health in Sweet's Girl), sent periodically while a synced mission runs
struct SOnscreenCounterState
{
    bool enabled{};
    int32_t varOffset{};  // byte offset of the global var in CTheScripts::ScriptSpace
    char textKey[10]{};
    uint8_t type{};       // 0 number, 1 bar, 2 number/number
    uint8_t colour{};
    int32_t value{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, enabled);
        if (enabled)
        {
            serialize_int(stream, varOffset, 0, 200000 - (int)sizeof(int));
            serialize_string(stream, textKey, 10);
            textKey[ARRAY_SIZE(textKey) - 1] = '\0';
            serialize_int(stream, type, 0, 2);
            serialize_uint8(stream, colour);
            serialize_int(stream, value, -100000000, 100000000);
        }
        return true;
    }
};

class OnscreenTimerSync : public Packet
{
    DEFINE_PACKET_TYPE(OnscreenTimerSync, ePacketType::ONSCREEN_TIMER_SYNC, ePacketChannel::SCRIPT);

public:
    bool enabled{};
    int32_t varOffset{};  // byte offset of the global var in CTheScripts::ScriptSpace
    char textKey[10]{};
    uint8_t direction{};
    int32_t value{};
    bool paused{};
    SOnscreenCounterState counters[4]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, enabled);
        serialize_int(stream, varOffset, 0, 200000 - (int)sizeof(int));
        serialize_string(stream, textKey, 10);
        textKey[ARRAY_SIZE(textKey) - 1] = '\0';
        serialize_int(stream, direction, 0, 1);
        serialize_int(stream, value, -100000000, 100000000);
        serialize_bool(stream, paused);
        for (auto& counter : counters)
        {
            serialize_object(stream, counter);
        }
        return true;
    }
};

class OpCodeSync : public Packet
{
    DEFINE_PACKET_TYPE(OpCodeSync, ePacketType::OPCODE_SYNC, ePacketChannel::SCRIPT);

public:
    // this number is the biggest possible serialized opcode in theory, the usual opcode size is ~4-80 bytes
    // sizeof(OpcodeSyncHeader)
    // + NUM_ENTITY_PARAMS * (entity type byte + packed 64-bit entity id)
    // + (NUM_SYNCED_PARAMS - NUM_ENTITY_PARAMS) * sizeof(int)
    // + NUM_SYNCED_PARAMS * sizeof(uint8_t) + NUM_SYNCED_PARAMS * 256
    // (see COpCodeSync.h, only the first 4 parameters can hold an entity)
    static constexpr int MAX_BUFFER_SIZE = 4 + 4 * (sizeof(uint8_t) + sizeof(uint64_t)) + (15 - 4) * sizeof(int) +
                                           15 * sizeof(uint8_t) + 15 * 256;

    int size = 0;
    uint8_t buffer[MAX_BUFFER_SIZE]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, size, 4, MAX_BUFFER_SIZE);
        serialize_bytes(stream, buffer, size);
        return true;
    }

public:
    std::string ToString() const override
    {
        static char str[32];
        snprintf(str, sizeof(str), "Opcode: 0x%x", *(uint16_t*)&buffer[0]);
        return str;
    }
};

class PerformTaskSequence : public Packet
{
    DEFINE_PACKET_TYPE(PerformTaskSequence, ePacketType::PERFORM_TASK_SEQUENCE, ePacketChannel::SCRIPT);

public:
    // header: playerid_t (filled by the server) + packed pedid_t + uint8_t task count
    static constexpr int HEADER_SIZE = ids::PLAYERID_SIZE + ids::ENTITYID_SIZE + sizeof(uint8_t);
    static constexpr int MAX_BUFFER_SIZE = HEADER_SIZE + 8 * 256;

    int size = 0;
    uint8_t buffer[MAX_BUFFER_SIZE]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, size, HEADER_SIZE, MAX_BUFFER_SIZE);
        serialize_bytes(stream, buffer, size);
        return true;
    }
};

class LaunchMissionLocal : public Packet
{
    DEFINE_PACKET_TYPE(LaunchMissionLocal, ePacketType::LAUNCH_MISSION_LOCAL, ePacketChannel::SCRIPT);

public:
    int missionId = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, missionId, 0, 200);
        return true;
    }
};

// one story/side mission at a time per mission id (docs/REQUISITOS.md 1): the player that starts a mission claims its id, the
// server keeps the owners and tells everybody. Not an authority change: only who may START that mission.
class MissionLockRequest : public Packet
{
    DEFINE_PACKET_TYPE(MissionLockRequest, ePacketType::MISSION_LOCK_REQUEST, ePacketChannel::SCRIPT);

public:
    int missionId = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, missionId, 0, 200);
        return true;
    }
};

class MissionLockRelease : public Packet
{
    DEFINE_PACKET_TYPE(MissionLockRelease, ePacketType::MISSION_LOCK_RELEASE, ePacketChannel::SCRIPT);

public:
    int missionId = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, missionId, 0, 200);
        return true;
    }
};

// server -> clients: who owns the mission now (locked = false: free). Also the answer to a denied request.
class MissionLockState : public Packet
{
    DEFINE_PACKET_TYPE(MissionLockState, ePacketType::MISSION_LOCK_STATE, ePacketChannel::SCRIPT);

public:
    int missionId = 0;
    bool locked = false;
    playerid_t ownerId{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, missionId, 0, 200);
        serialize_bool(stream, locked);
        serialize_playerid(stream, ownerId);
        return true;
    }
};

// the script authority -> the others: the story flags (small global vars of main.scm) that the coop story mission it just
// passed changed. A receiver applies an entry only if his value is still the old one, see CStoryProgress
class StoryProgressSync : public Packet
{
    DEFINE_PACKET_TYPE(StoryProgressSync, ePacketType::STORY_PROGRESS_SYNC, ePacketChannel::SCRIPT);

public:
    static constexpr int MAX_ENTRIES = 256;

    struct SEntry
    {
        int32_t offset = 0;  // byte offset of the global var in CTheScripts::ScriptSpace
        uint8_t oldValue = 0;
        uint8_t newValue = 0;
    };

    uint16_t count = 0;
    SEntry entries[MAX_ENTRIES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, count, 0, MAX_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_int(stream, entries[i].offset, 0, 200000);
            serialize_int(stream, entries[i].oldValue, 0, 255);
            serialize_int(stream, entries[i].newValue, 0, 255);
        }
        return true;
    }
};

// client -> server -> the other clients: a player plays the missions alone (individual) or together (coop), F1 menu
class PlayerMissionMode : public Packet
{
    DEFINE_PACKET_TYPE(PlayerMissionMode, ePacketType::PLAYER_MISSION_MODE, ePacketChannel::SCRIPT);

public:
    playerid_t playerId{};  // filled by the server
    bool individual = false;
    int sweetProgress = -1;  // $flag_sweet_mission_counter of the sender (-1: unknown), see CMissionMode
    int runningMission = -1;  // the id of the mission the sender runs (-1: none), see CVsRace
    bool vanillaScripts = false;  // the sender plays with the original main.scm (vanilla-scripts, CConfigLoader)

    static constexpr int STORY_CHAIN_COUNT = 24;
    static constexpr uint8_t CHAIN_UNKNOWN = 255;
    uint8_t chainProgress[STORY_CHAIN_COUNT]{};  // the counter of each story chain (CMissionMode), CHAIN_UNKNOWN: unknown
    uint32_t storyLaunchers = 0;  // bit i: the launcher thread of story chain i runs in the sender's game (CMissionMode)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerId);
        serialize_bool(stream, individual);
        serialize_int(stream, sweetProgress, -1, 255);
        serialize_int(stream, runningMission, -1, 255);
        serialize_bool(stream, vanillaScripts);
        for (int i = 0; i < STORY_CHAIN_COUNT; i++)
            serialize_int(stream, chainProgress[i], 0, 255);
        serialize_uint32(stream, storyLaunchers);
        return true;
    }
};

// the lobby of a VS race (CVsLobby): the player that enters the race marker first invites the others, who accept and enter the
// marker too; the race starts for everybody at once. The server only relays it to the other players.
class RaceVsLobby : public Packet
{
    DEFINE_PACKET_TYPE(RaceVsLobby, ePacketType::RACE_VS_LOBBY, ePacketChannel::SCRIPT);

public:
    enum eAction : uint8_t
    {
        INVITE = 0,   // owner -> all
        ACCEPT = 1,   // guest -> all
        DECLINE = 2,  // guest -> all
        ARRIVED = 3,  // guest -> all: he stands in the race marker and waits
        GO = 4,       // owner -> all: everybody who arrived starts the race now
        CANCEL = 5,   // owner -> all: no race
    };

    playerid_t playerid{};  // filled by the server with the sender
    uint8_t action = 0;
    server_time_t startTime = 0;  // GO only: server-clock instant at which every client starts the race (0 = at once)
    uint8_t mode = 0;             // INVITE / GO: 0 players only, 1 NPC opponents too (the owner's)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_uint8(stream, action);
        serialize_uint32(stream, startTime);
        serialize_int(stream, mode, 0, 1);
        return true;
    }
};

class MinigameVsScore : public Packet
{
    DEFINE_PACKET_TYPE(MinigameVsScore, ePacketType::MINIGAME_VS_SCORE, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};  // filled by the server with the sender
    uint8_t gameId = 0;
    int32_t score = 0;
    uint8_t state = 0;  // 0 playing, 1 finished, 2 abandoned

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_uint8(stream, gameId);
        serialize_int(stream, score, -100000000, 100000000);
        serialize_uint8(stream, state);
        return true;
    }
};

// the host's active script camera during a coop mission scene (Running Dog, Drive-By...): fixed camera position,
// look direction, up vector and FOV, so every client sees the same shot
class CameraMirror : public Packet
{
    DEFINE_PACKET_TYPE(CameraMirror, ePacketType::CAMERA_MIRROR, ePacketChannel::SYNC);

public:
    bool active = false;
    bool wide = false;  // the authority has the widescreen bars on during its mission (sent with or without the camera)
    WorldPositionCompressed source{};
    NormalizedVector front{};
    NormalizedVector up{};
    float fov = 70.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, active);
        serialize_bool(stream, wide);
        if (active)
        {
            serialize_object(stream, source);
            serialize_object(stream, front);
            serialize_object(stream, up);
            serialize_compressed_float(stream, fov, 1.0f, 180.0f, 0.01f);
        }
        return true;
    }
};

// F: cooperative side mission. The player that starts the side mission is its "owner": his game runs the script and
// the server keeps who owns it and who joined. One mission at a time; while it is active the owner, not the host, is
// the script authority. The client offers it, the server reserves the slot and invites everyone else.
class CoopMissionOffer : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionOffer, ePacketType::COOP_MISSION_OFFER, ePacketChannel::SCRIPT);

public:
    char scriptName[8]{};  // e.g. "COPCAR"
    char title[24]{};      // e.g. "Vigilante"

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_string(stream, scriptName, 8);
        scriptName[ARRAY_SIZE(scriptName) - 1] = '\0';
        serialize_string(stream, title, 24);
        title[ARRAY_SIZE(title) - 1] = '\0';
        return true;
    }
};

class CoopMissionInvite : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionInvite, ePacketType::COOP_MISSION_INVITE, ePacketChannel::SCRIPT);

public:
    playerid_t owner{};
    char scriptName[8]{};
    char title[24]{};
    uint16_t timeoutMs = 12000;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, owner);
        serialize_string(stream, scriptName, 8);
        scriptName[ARRAY_SIZE(scriptName) - 1] = '\0';
        serialize_string(stream, title, 24);
        title[ARRAY_SIZE(title) - 1] = '\0';
        serialize_uint16(stream, timeoutMs);
        return true;
    }
};

class CoopMissionReply : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionReply, ePacketType::COOP_MISSION_REPLY, ePacketChannel::SCRIPT);

public:
    bool accept = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, accept);
        return true;
    }
};

class CoopMissionState : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionState, ePacketType::COOP_MISSION_STATE, ePacketChannel::SCRIPT);

public:
    static constexpr int MAX_PARTICIPANTS = 4;

    enum ePhase : uint8_t
    {
        FREE = 0,
        OFFER = 1,
        RUNNING = 2,
    };

    enum eReason : uint8_t
    {
        OK = 0,
        BUSY = 1,
        NOBODY_ACCEPTED = 2,
        OWNER_LEFT = 3,
        DISCONNECT = 4,
        FINISHED = 5,
        PARTICIPANT_FELL = 6,  // a guest died or was arrested: the owner fails the mission for everyone
    };

    uint8_t phase = FREE;
    playerid_t owner{};
    char scriptName[8]{};
    char title[24]{};
    uint8_t count = 0;
    playerid_t participants[MAX_PARTICIPANTS]{};
    uint8_t reason = OK;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, phase, FREE, RUNNING);
        serialize_playerid(stream, owner);
        serialize_string(stream, scriptName, 8);
        scriptName[ARRAY_SIZE(scriptName) - 1] = '\0';
        serialize_string(stream, title, 24);
        title[ARRAY_SIZE(title) - 1] = '\0';
        serialize_int(stream, count, 0, MAX_PARTICIPANTS);
        for (int i = 0; i < count; i++)
        {
            serialize_playerid(stream, participants[i]);
        }
        serialize_int(stream, reason, OK, PARTICIPANT_FELL);
        return true;
    }
};

class CoopMissionLeave : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionLeave, ePacketType::COOP_MISSION_LEAVE, ePacketChannel::SCRIPT);

public:
    bool fell = false;  // left because he died or was arrested (not on his own will)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, fell);
        return true;
    }
};

class CoopMissionEnd : public Packet
{
    DEFINE_PACKET_TYPE(CoopMissionEnd, ePacketType::COOP_MISSION_END, ePacketChannel::SCRIPT);

public:
    bool passed = false;  // informative only: the mission ended (passed or failed) on the owner's game
    bool cancelled = false;  // the owner gave it up on purpose (holding "2"): the others are told

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_bool(stream, passed);
        serialize_bool(stream, cancelled);
        return true;
    }
};

// A single story-script operation on each participant's own CJ; never a join snapshot.
class MissionPlayerEquipment : public Packet
{
    DEFINE_PACKET_TYPE(MissionPlayerEquipment, ePacketType::MISSION_PLAYER_EQUIPMENT, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};
    uint8_t weaponType = 0;
    int32_t ammo = 0;
    bool equip = false;
    bool remove = false;
    bool setAmmo = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_int(stream, weaponType, 0, 46);
        serialize_int(stream, ammo, 0, INT32_MAX);
        serialize_bool(stream, equip);
        serialize_bool(stream, remove);
        serialize_bool(stream, setAmmo);
        return (weaponType < 19 || weaponType > 21) && !(equip && remove) && !(setAmmo && (equip || remove));
    }
};

// Ordered before the existing WARP rule-teleport on the SCRIPT channel.
class MissionScriptedExit : public Packet
{
    DEFINE_PACKET_TYPE(MissionScriptedExit, ePacketType::MISSION_SCRIPTED_EXIT, ePacketChannel::SCRIPT);

public:
    playerid_t playerid{};
    uint16_t opcode = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, playerid);
        serialize_int(stream, opcode, 0, 0x7FFF);
        return opcode == 0x05CD || opcode == 0x0633 || opcode == 0x0622 || opcode == 0x0362;
    }
};

// Team lowrider contest; session separates repeated attempts, sender is stamped by the server.
class CoopLowrider : public Packet
{
    DEFINE_PACKET_TYPE(CoopLowrider, ePacketType::COOP_LOWRIDER, ePacketChannel::SCRIPT);
public:
    enum : uint8_t { PRELOAD = 1, PLAY, STOP, TIME, JOIN, POINTS, DONE };
    playerid_t owner{}, sender{};
    uint32_t session = 0;
    uint8_t action = PRELOAD;
    uint8_t kind = 1; // 1 lowrider, 2 MUSIC1 dance, 3 SWEET6 preparation
    playerid_t garageOwner{};
    int32_t wager[3]{};
    uint8_t colors[2]{};
    float cameras[30]{};
    int32_t value = 0, beat = 0;
    CVector pos{};
    float heading = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, owner);
        serialize_playerid(stream, sender);
        serialize_uint32(stream, session);
        serialize_int(stream, action, PRELOAD, DONE);
        serialize_int(stream, kind, 1, 3);
        if (kind == 3) {
            serialize_playerid(stream, garageOwner);
            for (auto& amount : wager) serialize_int(stream, amount, 0, 10000000);
            for (auto& color : colors) serialize_uint8(stream, color);
        }
        if (kind == 2 && action == PRELOAD)
            for (auto& camera : cameras) serialize_float(stream, camera);
        serialize_int(stream, value, 0, 3600000);
        serialize_int(stream, beat, 0, 100000);
        serialize_vector(stream, pos);
        serialize_float(stream, heading);
        return session != 0 && (action != PRELOAD || (kind == 3 ? value == 567 : value < 14)) && (action != POINTS || value <= (kind == 2 ? 1000000 : 500));
    }
};

// Native POOL2 session, independent of story mission authority. Sender is server-stamped.
class CoopPool : public Packet
{
public:
    enum : uint8_t { OFFER = 1, JOIN, START, END, INPUT, VIEW, RESULT };
    ePacketType GetType() override { return ePacketType::COOP_POOL; }
    ePacketChannel GetChannel() override
    {
        return action == VIEW || action == INPUT ? ePacketChannel::SYNC : ePacketChannel::SCRIPT;
    }
    bool SerializeRead(serialize::ReadStream& stream) override
    {
        bool result = Serialize(stream);
        if (result) m_bytesRead = stream.GetBytesProcessed();
        return result;
    }
    bool SerializeWrite(serialize::WriteStream& stream) override { return Serialize(stream); }
    bool SerializeMeasure(serialize::MeasureStream& stream) override { return Serialize(stream); }
    Packet* Clone() const override { return new CoopPool(*this); }
    inline static PacketRegistrar<CoopPool> _registrar;
    playerid_t owner{}, sender{}, partner{};
    uint32_t session = 0, sequence = 0;
    uint8_t action = OFFER, turn = 0, count = 0;
    CVector pos{}, camera{}, front{};
    float heading = 0, fov = 70, mouseX = 0, mouseY = 0;
    bool joypad = false;
    int16_t keys[20]{};
    struct Anim {
        char block[16]{};
        uint32_t hash = 0;
        float time = 0, speed = 1;
        uint16_t flags = 0;
    } anim;
    struct Object {
        uint8_t slot = 0;
        uint16_t model = 0;
        CVector pos{}, right{}, up{}, at{};
        float scale = 1;
        bool visible = false;
        Anim anim{};
    } objects[75];
    CVector tablePos{}, tableRight{}, tableUp{}, tableAt{};
    uint16_t tableModel = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_playerid(stream, owner); serialize_playerid(stream, sender); serialize_playerid(stream, partner);
        serialize_uint32(stream, session); serialize_uint32(stream, sequence);
        serialize_int(stream, action, OFFER, RESULT);
        serialize_vector(stream, pos);
        if (action == INPUT) {
            for (auto& k : keys) serialize_int(stream, k, -32768, 32767);
            serialize_float(stream, mouseX); serialize_float(stream, mouseY); serialize_bool(stream, joypad);
        }
        if (action == VIEW || action == RESULT) serialize_int(stream, turn, 0, 1);
        if (action == VIEW) {
            serialize_int(stream, tableModel, 0, 19999);
            serialize_vector(stream, tablePos); serialize_vector(stream, tableRight);
            serialize_vector(stream, tableUp); serialize_vector(stream, tableAt);
            serialize_vector(stream, camera); serialize_vector(stream, front);
            serialize_float(stream, fov); serialize_float(stream, heading);
            if (!SerializeAnim(stream, anim)) return false;
            serialize_int(stream, count, 0, 75);
            for (int i = 0; i < count; ++i) {
                auto& o = objects[i];
                serialize_int(stream, o.slot, 0, 74); serialize_int(stream, o.model, 0, 19999);
                serialize_vector(stream, o.pos); serialize_vector(stream, o.right);
                serialize_vector(stream, o.up); serialize_vector(stream, o.at);
                serialize_float(stream, o.scale); serialize_bool(stream, o.visible);
                // Most balls have no animation: skip its ~30 bytes unless there is one.
                bool hasAnim = o.anim.hash != 0;
                serialize_bool(stream, hasAnim);
                if (hasAnim) { if (!SerializeAnim(stream, o.anim)) return false; }
                else o.anim = Anim{};
            }
        }
        return session != 0;
    }
    template <typename Stream> bool SerializeAnim(Stream& stream, Anim& a) {
        for (auto& c : a.block) { uint8_t b = static_cast<uint8_t>(c); serialize_int(stream, b, 0, 127); c = char(b); }
        serialize_uint32(stream, a.hash); serialize_float(stream, a.time); serialize_float(stream, a.speed);
        serialize_int(stream, a.flags, 0, 65535);
        return a.block[15] == 0;
    }
};

// Independent activity scores; the server binds exactly two consenting players.
class ActivityChallenge : public Packet {
    DEFINE_PACKET_TYPE(ActivityChallenge, ePacketType::ACTIVITY_CHALLENGE, ePacketChannel::SCRIPT);
public:
    enum : uint8_t { OFFER = 1, ACCEPT, DECLINE, STATE, LEAVE };
    playerid_t owner{}, partner{}, sender{};
    uint32_t session = 0;
    uint8_t action = OFFER, kind = 1, state = 0; // waiting, playing, finished, abandoned
    int32_t score = 0;
    int32_t variant = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, owner); serialize_playerid(stream, partner); serialize_playerid(stream, sender);
        serialize_uint32(stream, session);
        serialize_int(stream, action, OFFER, LEAVE); serialize_int(stream, kind, 1, 7);
        serialize_int(stream, state, 0, 3); serialize_int(stream, score, -100000000, 100000000);
        serialize_int(stream, variant, 0, 2);
        return session != 0 && owner != partner;
    }
};


// A mission owner's decision for one camera shot (MIT ScenePartner adaptation).
class CutscenePartners : public Packet {
    DEFINE_PACKET_TYPE(CutscenePartners, ePacketType::CUTSCENE_PARTNERS, ePacketChannel::SCRIPT);
public:
    playerid_t owner{}, sender{};
    uint8_t kind = 0;
    uint32_t session = 0, sequence = 0, shot = 0;
    char scene[9]{};
    float at = 0, heading = 0;
    CVector ground{}, via{}, seat[2]{};
    uint8_t pose = 4, act = 0, seats = 0, count = 0;
    playerid_t partners[3]{};
    bool walk = false, viaSet = false, active = true;
private:
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, owner); serialize_playerid(stream,sender); serialize_int(stream,kind,0,1); serialize_uint32(stream, session); serialize_uint32(stream, sequence); serialize_uint32(stream,shot);
        for (auto& c : scene) { uint8_t b = uint8_t(c); serialize_int(stream,b,0,127); c=char(b); }
        serialize_float(stream,at); serialize_float(stream,heading);
        serialize_vector(stream,ground); serialize_vector(stream,via);
        for (auto& s:seat) serialize_vector(stream,s);
        serialize_int(stream,pose,0,7); serialize_int(stream,act,0,255); serialize_int(stream,seats,0,2);
        serialize_bool(stream,walk); serialize_bool(stream,viaSet); serialize_bool(stream,active);
        serialize_int(stream,count,0,3);
        for(int i=0;i<count;++i) serialize_playerid(stream,partners[i]);
        return session != 0 && scene[8] == 0;
    }
};

// Directed consent. START is server-only; replies must match the stored session.
class SocialAction : public Packet {
    DEFINE_PACKET_TYPE(SocialAction, ePacketType::SOCIAL_ACTION, ePacketChannel::SCRIPT);
public:
    enum : uint8_t { OFFER = 1, ACCEPT, CANCEL, START };
    enum : uint8_t { GO = 1, BRING, PAIR };
    playerid_t owner{}, partner{};
    uint32_t session = 0;
    uint8_t action = OFFER, kind = GO, emote = 0, reason = 0;
    CVector pos{};
    float heading = 0;
    uint8_t area = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, owner); serialize_playerid(stream, partner);
        serialize_uint32(stream, session); serialize_int(stream, action, OFFER, START);
        serialize_int(stream, kind, GO, PAIR); serialize_int(stream, emote, 0, 15);
        serialize_int(stream, reason, 0, 7); serialize_vector(stream, pos);
        serialize_float(stream, heading); serialize_int(stream, area, 0, 255);
        return session != 0 && owner != partner && std::isfinite(pos.x) && std::isfinite(pos.y) &&
            std::isfinite(pos.z) && std::isfinite(heading);
    }
};
// Same block name / hierarchy key used by the existing named NPC animation sync.
class EmoteState : public Packet {
    DEFINE_PACKET_TYPE(EmoteState, ePacketType::EMOTE_STATE, ePacketChannel::SCRIPT);
public:
    playerid_t player{};
    uint32_t session = 0, sequence = 0, hash = 0;
    char block[16]{};
    float time = 0;
    bool active = false;
private:
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, player); serialize_uint32(stream, session); serialize_uint32(stream, sequence);
        serialize_uint32(stream, hash); serialize_float(stream, time); serialize_bool(stream, active);
        for (auto& c : block) { uint8_t b = uint8_t(c); serialize_int(stream, b, 0, 127); c = char(b); }
        return session != 0 && block[15] == 0;
    }
};

// High Stakes: only the authority measures progress; the target draws its own next checkpoint.
class StoryRace : public Packet {
    DEFINE_PACKET_TYPE(StoryRace, ePacketType::STORY_RACE, ePacketChannel::SCRIPT);
public:
    enum Phase : uint8_t { PREPARE, COUNTDOWN, RACING, FINISHED, END };
    playerid_t owner{}, target{};
    uint32_t session = 0, sequence = 0;
    uint8_t phase = PREPARE, type = 0, place = 1, total = 1, lap = 1, laps = 1;
    float next[3]{}, after[3]{}, grid[3]{}, size = 6.0f, heading = 0.0f;
    // Standings table (best first) drawn by every racer: NPC racers, the authority and the guests.
    static constexpr int BOARD_MAX = 10;
    struct BoardRow { char name[20]{}; uint8_t kind = 0, lap = 1, done = 0; };  // kind: 0 NPC, 1 player
    uint8_t rows = 0;
    BoardRow board[BOARD_MAX]{};
private:
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, owner); serialize_playerid(stream, target);
        serialize_uint32(stream, session); serialize_uint32(stream, sequence);
        serialize_int(stream, phase, 0, END); serialize_int(stream, type, 0, 8);
        serialize_int(stream, place, 1, 24); serialize_int(stream, total, 1, 24);
        serialize_int(stream, lap, 1, 20); serialize_int(stream, laps, 1, 20);
        for (int i = 0; i < 3; ++i) {
            serialize_float(stream, next[i]); serialize_float(stream, after[i]); serialize_float(stream, grid[i]);
        }
        serialize_float(stream, size); serialize_float(stream, heading);
        serialize_int(stream, rows, 0, BOARD_MAX);
        for (int i = 0; i < rows; ++i) {
            serialize_string(stream, board[i].name, sizeof(board[i].name));
            board[i].name[sizeof(board[i].name) - 1] = '\0';
            serialize_int(stream, board[i].kind, 0, 1); serialize_int(stream, board[i].lap, 0, 20);
            serialize_int(stream, board[i].done, 0, 1);
        }
        return session != 0 && sequence != 0;
    }
};

}  // namespace Packets::Scripts
