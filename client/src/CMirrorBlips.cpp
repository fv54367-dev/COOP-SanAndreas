#include "CHighStakesRace.h"
// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0), package M2, and adapted.
#include "stdafx.h"
#include "CObjectSync.h"
#include "CMirrorBlips.h"
#include "CConfigLoader.h"
#include "CMirrorBlipsPolicy.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "COpCodeSync.h"
#include "CNetworkStaticBlip.h"
#include "CServerTime.h"
#include "network/packets/mirror_blips.h"
#include <CEntryExit.h>
#include <CEntryExitManager.h>
#include <CPickups.h>
#include <CPools.h>
#include <CTimer.h>
#include <cstdarg>

// The mission mirror layer's blips, see CMirrorBlips.h.
//
// Adapted from the source: "host" is the script authority (CCoopMission::IsScriptAuthority); its session events
// (CSessionLifecycle) are polled here once per frame instead (authority change, disconnection, the frame counter going
// back on a restart/load); objects are not resolved by id (no CMirrorObjects) and trains never are (no CTrainSync):
// both follow the authority's position, as the source does for an entity it can't find.
//
// gta_sa.exe 1.0 US used below (checked against gta-reversed and the 1.0 US exe with capstone, 2026-10-25):
// - The creating commands (cases found through CRunningScript::CommandHandlerTable 0x8A6168): each case ends with
//   `mov [0A43C78h],reg` (ScriptParams[0], 0xA43C78) and CRunningScript::StoreParameters 0x464370 with 1, so the handle
//   is in ScriptParams[0] when CRunningScript::Process's command call returns: 0161 case 0x47C610 (store at 0x47C64C),
//   0167 0x47C6AA (0x47C750), 0186 0x47CEBF, 0187 0x47CEFD, 0188 0x47CF25 and 018A 0x47CF4E (shared store at
//   0x47CFF6, checked: `mov [0xa43c78], edi` after SetEntityBlip 0x47CEE6 / SetCoordBlip 0x47CFE0), 02A7 0x47F6BD and
//   02A8 0x47F75D (0x47F80A), 03DC 0x4854B9 (0x485504), 04CE 0x48BC0A (0x48BCB4), 0570 0x48DB3D (0x48DBED), 06C4
//   0x497BC7 (0x497C14), 0888 0x471D2B (0x471D68). RadarHooks redirects some of the SetCoordBlip calls there; the hook
//   returns the same handle, so the store is unchanged. A handle is CRadar's (trace index | counter << 16), -1 when no
//   trace was free (CRadar::SetCoordBlip 0x583820, SetEntityBlip 0x5839A0). CRadar::GetActualBlipArrayIndex 0x582870
//   checks the counter and the in-use bit (gta-reversed Radar.cpp).
// - tRadarTrace (plugin-sdk CRadar.h; the static_asserts below): colour u32 +0x00, entity handle +0x04, position +0x08,
//   counter u16 +0x14, size u16 +0x1C, CEntryExit* +0x20, sprite +0x24. The byte at +0x25: bright, in use, short range,
//   friendly, remain, fade, coordinate appearance (2 bits). The byte at +0x26: display bits 0-1, type bits 2-5. The
//   fields are written here directly, as CNetworkStaticBlip::Create does, after GetActualBlipArrayIndex checked them.
// - Where the radar draws an entity blip (CRadar::DrawEntityBlip 0x587000): car at the vehicle; char at the ped, or at
//   its vehicle while it is in one; object; searchlight at CTheScripts::ScriptSearchLightArray (0xA94D68, index
//   CTheScripts::GetActualScriptThingIndex(handle, 2) 0x4839A0) target; pickup at CPickups::aPickUps (0x9788C0, index
//   CPickups::GetActualPickupIndex 0x4552A0) through CPickup::GetPosn 0x4549A0. Coordinate types are drawn at the trace
//   position, moved by its entrance.
// - Entrances: set_blip_entry_exit 08DC (case 0x475838, checked) calls CEntryExitManager::FindNearestEntryExit 0x43F4B0
//   with (&{x, y}, radius, 0), CEntryExitManager::GetEntryExit 0x43EF00 on its result and SetBlipEntryExit 0x583F00 at
//   0x475882 (also hooked by RadarHooks).
// - Entity destructors clear their blips (CRadar::ClearBlipForEntity 0x587C60), so an entity blip whose entity is gone
//   stops resolving: removed on the authority, rebuilt on the others.
// - The frame counter starts over with a new game or a load: CTimer::Initialise 0x5617E0 zeroes CTimer::m_FrameCounter
//   0xB7CB4C (checked: `mov [0xb7cb4c], eax` at 0x561814 with eax = 0).

static_assert(offsetof(tRadarTrace, m_nColour) == 0x00, "tRadarTrace::m_nColour");
static_assert(offsetof(tRadarTrace, m_nEntityHandle) == 0x04, "tRadarTrace::m_nEntityHandle");
static_assert(offsetof(tRadarTrace, m_vecPos) == 0x08, "tRadarTrace::m_vecPos");
static_assert(offsetof(tRadarTrace, m_nCounter) == 0x14, "tRadarTrace::m_nCounter");
static_assert(offsetof(tRadarTrace, m_nBlipSize) == 0x1C, "tRadarTrace::m_nBlipSize");
static_assert(offsetof(tRadarTrace, m_pEntryExit) == 0x20, "tRadarTrace::m_pEntryExit");
static_assert(offsetof(tRadarTrace, m_nRadarSprite) == 0x24, "tRadarTrace::m_nRadarSprite");
static_assert(sizeof(tRadarTrace) == 0x28, "tRadarTrace");
static_assert(MirrorBlipsPolicy::MAX_BLIPS == MAX_RADAR_TRACES, "one owned or mirrored blip per radar trace at most");
static_assert(MirrorBlipsPolicy::Type::CAR == BLIP_CAR && MirrorBlipsPolicy::Type::CHAR == BLIP_CHAR &&
                  MirrorBlipsPolicy::Type::OBJECT == BLIP_OBJECT && MirrorBlipsPolicy::Type::COORD == BLIP_COORD &&
                  MirrorBlipsPolicy::Type::CONTACT_POINT == BLIP_CONTACTPOINT &&
                  MirrorBlipsPolicy::Type::SPOTLIGHT == BLIP_SPOTLIGHT &&
                  MirrorBlipsPolicy::Type::PICKUP == BLIP_PICKUP && MirrorBlipsPolicy::Type::AIRSTRIP == BLIP_AIRSTRIP,
    "eBlipType");
static_assert(MirrorBlipsPolicy::Type::CAR == MirrorBlipsWire::TYPE_FIRST &&
                  MirrorBlipsPolicy::Type::AIRSTRIP == MirrorBlipsWire::TYPE_LAST,
    "the wire's blip types");
static_assert(static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::NONE) == MirrorBlipsWire::ENTITY_NONE &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::PLAYER) == MirrorBlipsWire::ENTITY_PLAYER &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::PED) == MirrorBlipsWire::ENTITY_PED &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::VEHICLE) == MirrorBlipsWire::ENTITY_VEHICLE &&
                  static_cast<uint8_t>(MirrorBlipsPolicy::eEntityKind::OBJECT) == MirrorBlipsWire::ENTITY_OBJECT,
    "the wire's entity kinds");

namespace
{
using MirrorBlipsPolicy::eEntityKind;
using MirrorBlipsPolicy::eLocalForm;
using MirrorBlipsPolicy::eSend;
using MirrorBlipsPolicy::SBlipState;
using MirrorBlipsPolicy::SMirroredBlip;
using MirrorBlipsPolicy::SOwnedBlip;

// how often a receiver checks every mirrored blip (an entity that resolved, a local blip the game cleared)
constexpr uint32_t RECONCILE_INTERVAL_MS = 250;
// FindNearestEntryExit range for a mirrored entrance: the authority sends the exact middle of the entrance rectangle
constexpr float ENTRANCE_SEARCH_RANGE = 1.0f;
// set_blip_entry_exit's ignoreArea (the 08DC case pushes 0)
constexpr int ENTRANCE_IGNORE_AREA = 0;
// join snapshots waiting for the authority's next frame
constexpr int MAX_PENDING_SNAPSHOTS = 16;

// authority: the blips the mission family created
MirrorBlipsPolicy::COwnedBlips ms_owned;
// receivers: the authority's blips and this game's copies of them
MirrorBlipsPolicy::CMirroredBlips ms_mirrored;
// receivers: the authority whose blips ms_mirrored holds
playerid_t ms_mirrorSenderId{};

uint32_t ms_nLastFrame = 0;          // CTimer::m_FrameCounter at the last call (a smaller one = the game restarted)
bool ms_bFrameSeen = false;
uint32_t ms_nLastReconcile = 0;      // GetTickCount
bool ms_bReconcileNow = false;
bool ms_bBroadcastPending = false;   // authority: its whole set with bReset to everyone (after its game restarted)
playerid_t ms_aPendingSnapshots[MAX_PENDING_SNAPSHOTS]{};  // authority: join snapshots still to send
int ms_nPendingSnapshots = 0;

// --coopdiag (plain values; the diag thread reads them)
struct SCounters
{
    unsigned int owned = 0;           // authority: owned now
    unsigned int ownedTotal = 0;
    unsigned int ownFailures = 0;     // a family's creating command left no handle (no free trace), or the set is full
    unsigned int sentFull = 0;
    unsigned int sentPosition = 0;
    unsigned int sentRemoved = 0;
    unsigned int sentPackets = 0;
    unsigned int snapshots = 0;       // join snapshots
    unsigned int broadcasts = 0;      // the whole set to everyone with bReset (restart, load, authority lost)
    unsigned int captureFailures = 0; // an owned entity blip whose entity wasn't found in a frame
    unsigned int staticSkipped = 0;   // host: owned traces left out of the static blip snapshot
    unsigned int mirrored = 0;        // receiver: mirrored now
    unsigned int formEntity = 0;
    unsigned int formFollow = 0;
    unsigned int formCoord = 0;
    unsigned int formNone = 0;
    unsigned int recvPackets = 0;
    unsigned int recvDropped = 0;     // not from the current authority, or this player plays alone / left
    unsigned int recvResets = 0;
    unsigned int recvFull = 0;
    unsigned int recvPosition = 0;
    unsigned int recvRemoved = 0;
    unsigned int recvUnknown = 0;     // a position or removal for a blip this game doesn't have
    unsigned int rebuilds = 0;
    unsigned int switches = 0;        // follow -> entity blip
    unsigned int createFailures = 0;  // no free radar trace for a local blip
    unsigned int staticKept = 0;      // receiver: mirror traces the static blip snapshot left alone
};
SCounters ms_counters;

// CoopAndreas_mirror.log (through CMissionMirror), at most 256 records of this module
void LogLine(const char* format, ...)
{
    static unsigned int records = 0;
    if (records >= 256)
    {
        return;
    }
    ++records;
    char text[300];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    CMissionMirror::LogEventf("blips %s owned=%d mirrored=%d", text, ms_owned.Count(), ms_mirrored.Count());
}

const char* FormName(eLocalForm form)
{
    switch (form)
    {
        case eLocalForm::ENTITY: return "entity";
        case eLocalForm::FOLLOW: return "follow";
        case eLocalForm::COORD: return "coord";
        default: return "none";
    }
}

bool IsServerClockSynced()
{
    // packets sent before the first server-time answer carry the PC's uptime as their stamp and wait in the other games'
    // packet buffers; CServerTime::RecalculateOffset sets the offset from 0 on that answer
    return CServerTime::m_serverTimeOffset != 0;
}

// the player whose game runs the mission script, as the server sees it (server CCoopMission::IsScriptAuthority): the
// owner of a running coop mission, else the host. Invalid when unknown.
playerid_t GetAuthorityId()
{
    if (CCoopMission::IsRunning())
    {
        return CCoopMission::GetOwnerId();
    }
    if (CLocalPlayer::m_bIsHost)
    {
        return CNetworkPlayerManager::m_localPlayerId;
    }
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost)
        {
            return pNetworkPlayer->m_playerId;
        }
    }
    return playerid_t{};
}

// a receiver that plays alone (F1) or dropped out of the mission takes none of the authority's blips
bool IsReceiverOut()
{
    return CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft();
}

// ---- the authority ----

void SetEntity(SBlipState& state, eEntityKind kind, uint64_t id)
{
    if (id != 0)
    {
        state.entityKind = kind;
        state.entityId = id;
    }
}

// a ped's network identity by membership in the lists: a player's id, or a CNetworkPed's with a valid id (design 3.10)
void SetPedEntity(SBlipState& state, CPed* pPed)
{
    if (pPed->IsPlayer())
    {
        if (pPed == FindPlayerPed(0))
        {
            SetEntity(state, eEntityKind::PLAYER, CNetworkPlayerManager::m_localPlayerId.value());
        }
        else if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed))
        {
            SetEntity(state, eEntityKind::PLAYER, pNetworkPlayer->m_playerId.value());
        }
        return;
    }
    if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed))
    {
        if (pNetworkPed->m_pedId.valid())
        {
            SetEntity(state, eEntityKind::PED, pNetworkPed->m_pedId.packed());
        }
    }
}

void SetPosition(SBlipState& state, const CVector& position)
{
    state.x = position.x;
    state.y = position.y;
    state.z = position.z;
}

// The owned blip in trace `index` as the other games should show it; false when its entity isn't there this frame
// (nothing is sent for it then).
bool CaptureBlip(int index, SBlipState& state)
{
    const tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    state = SBlipState{};
    state.type = static_cast<uint8_t>(trace.m_nBlipType);
    if (!MirrorBlipsPolicy::IsValidType(state.type))
    {
        return false;
    }
    state.colour = trace.m_nColour;
    state.sprite = trace.m_nRadarSprite < 64 ? trace.m_nRadarSprite : 0;
    state.display = static_cast<uint8_t>(trace.m_nBlipDisplay);
    state.size = static_cast<uint8_t>(trace.m_nBlipSize < 255 ? trace.m_nBlipSize : 255);
    state.bBright = trace.m_bBright != 0;
    state.bShortRange = trace.m_bShortRange != 0;
    state.bFriendly = trace.m_bFriendly != 0;
    state.bRemain = trace.m_bBlipRemain != 0;
    state.bFade = trace.m_bBlipFade != 0;
    state.coordAppearance = static_cast<uint8_t>(trace.m_nCoordBlipAppearance);

    const int handle = static_cast<int>(trace.m_nEntityHandle);
    switch (state.type)
    {
        case MirrorBlipsPolicy::Type::CAR:
        {
            CVehicle* pVehicle = CPools::GetVehicle(handle);
            if (pVehicle == nullptr)
            {
                return false;
            }
            SetPosition(state, pVehicle->GetPosition());
            // without train sync the other games have no train copy: the blip follows the position
            if (pVehicle->m_nVehicleSubType != static_cast<unsigned int>(VEHICLE_TRAIN) ||
                CConfigLoader::ms_bTrainSync)
            {
                if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
                {
                    if (pNetworkVehicle->m_vehicleId.valid())
                    {
                        SetEntity(state, eEntityKind::VEHICLE, pNetworkVehicle->m_vehicleId.packed());
                    }
                }
            }
            return true;
        }
        case MirrorBlipsPolicy::Type::CHAR:
        {
            CPed* pPed = CPools::GetPed(handle);
            if (pPed == nullptr)
            {
                return false;
            }
            if (pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle != nullptr)
            {
                SetPosition(state, pPed->m_pVehicle->GetPosition());
            }
            else
            {
                SetPosition(state, pPed->GetPosition());
            }
            SetPedEntity(state, pPed);
            return true;
        }
        case MirrorBlipsPolicy::Type::OBJECT:
        {
            // Resolve the existing mission object identity; retain the fallback until announced.
            CObject* pObject = CPools::GetObject(handle);
            if (pObject == nullptr)
            {
                return false;
            }
            SetPosition(state, pObject->GetPosition());
            if (CConfigLoader::ms_bCoopHomeInvasion && CMissionMirror::GetRunningMissionId() == 24)
            {
                const uint32_t id = CObjectSync::GetNetworkId(handle);
                if (id)
                {
                    state.entityKind = eEntityKind::OBJECT;
                    state.entityId = id;
                }
            }
            return true;
        }
        case MirrorBlipsPolicy::Type::SPOTLIGHT:
        {
            const int light = CTheScripts::GetActualScriptThingIndex(handle, 2);
            if (light < 0)
            {
                return false;
            }
            const RwV3d& target = CTheScripts::ScriptSearchLightArray[light].target;
            SetPosition(state, CVector(target.x, target.y, target.z));
            return true;
        }
        case MirrorBlipsPolicy::Type::PICKUP:
        {
            const int pickup = CPickups::GetActualPickupIndex(handle);
            if (pickup < 0)
            {
                return false;
            }
            SetPosition(state, CPickups::aPickUps[pickup].GetPosn());
            return true;
        }
        default:  // coordinate, contact point (airstrip)
        {
            SetPosition(state, trace.m_vecPos);
            if (trace.m_pEntryExit != nullptr)
            {
                const CRect& rect = trace.m_pEntryExit->m_recEntrance;
                state.bEntrance = true;
                state.entranceX = (rect.left + rect.right) * 0.5f;
                state.entranceY = (rect.bottom + rect.top) * 0.5f;
                state.entranceZ = trace.m_pEntryExit->m_fEntranceZ;
            }
            return true;
        }
    }
}

void FillFull(Packets::Mirror::SMirrorBlipEntry& entry, uint32_t handle, const SBlipState& state)
{
    entry.hostHandle = handle;
    entry.op = MirrorBlipsWire::OP_FULL;
    entry.position = CVector(state.x, state.y, state.z);
    entry.type = state.type;
    entry.entityKind = static_cast<uint8_t>(state.entityKind);
    entry.entityId = state.entityKind != eEntityKind::NONE ? state.entityId : 0;
    entry.colour = state.colour;
    entry.sprite = state.sprite;
    entry.display = state.display;
    entry.size = state.size;
    entry.bBright = state.bBright;
    entry.bShortRange = state.bShortRange;
    entry.bFriendly = state.bFriendly;
    entry.bRemain = state.bRemain;
    entry.bFade = state.bFade;
    entry.coordAppearance = state.coordAppearance;
    entry.bEntrance = state.bEntrance;
    if (state.bEntrance)
    {
        entry.entrance = CVector(state.entranceX, state.entranceY, state.entranceZ);
    }
}

// MISSION_MIRROR_BLIPS packets of one target, filled entry by entry; a full packet goes out and the next one continues
// (bReset only on the first)
class CBatch
{
public:
    CBatch(playerid_t target, bool bReset) : m_target(target), m_bReset(bReset) { Packet().count = 0; }

    Packets::Mirror::SMirrorBlipEntry& Next()
    {
        Packets::Mirror::MirrorBlips& packet = Packet();
        if (packet.count >= MirrorBlipsWire::MAX_ENTRIES)
        {
            Send();
        }
        Packets::Mirror::SMirrorBlipEntry& entry = packet.entries[packet.count++];
        entry = Packets::Mirror::SMirrorBlipEntry{};
        return entry;
    }

    // bEvenEmpty: a reset goes out even without entries
    void Flush(bool bEvenEmpty)
    {
        if (Packet().count > 0 || (bEvenEmpty && !m_bSentAny))
        {
            Send();
        }
    }

private:
    static Packets::Mirror::MirrorBlips& Packet()
    {
        static Packets::Mirror::MirrorBlips packet{};  // 48 entries, kept off the stack
        return packet;
    }

    void Send()
    {
        Packets::Mirror::MirrorBlips& packet = Packet();
        packet.targetPlayerId = m_target;
        packet.bReset = m_bReset && !m_bSentAny;
        GetPacketFactory().Send(packet);
        ++ms_counters.sentPackets;
        m_bSentAny = true;
        packet.count = 0;
    }

    playerid_t m_target;
    bool m_bReset;
    bool m_bSentAny = false;
};

// The whole owned set with bReset (design 3.11): to a player who joined, or to everyone (invalid target) after this game
// restarted. What everyone got becomes the base of the next differences.
void SendSnapshot(playerid_t target)
{
    const bool bEveryone = !target.valid();
    const uint32_t now = GetTickCount();
    CBatch batch(target, true);
    int count = 0;
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        SOwnedBlip& blip = ms_owned.At(i);
        const int index = CRadar::GetActualBlipArrayIndex(static_cast<int>(blip.handle));
        SBlipState state;
        if (index < 0 || blip.bDisowned || !CaptureBlip(index, state))
        {
            continue;  // the next difference removes it, or sends it once its entity is there
        }
        FillFull(batch.Next(), blip.handle, state);
        ++count;
        if (bEveryone)
        {
            blip.bSent = true;
            blip.sent = state;
            blip.positionSentAt = now;
        }
    }
    batch.Flush(true);
    ms_counters.sentFull += static_cast<unsigned int>(count);
    if (bEveryone)
    {
        ++ms_counters.broadcasts;
    }
    else
    {
        ++ms_counters.snapshots;
    }
    LogLine("snapshot target=%u blips=%d", target.value(), count);
}

void SendPendingSnapshots()
{
    if (ms_bBroadcastPending)
    {
        ms_bBroadcastPending = false;
        SendSnapshot(playerid_t{});
    }
    const int pending = ms_nPendingSnapshots;
    ms_nPendingSnapshots = 0;
    for (int i = 0; i < pending; i++)
    {
        if (CNetworkPlayerManager::GetPlayer(ms_aPendingSnapshots[i]) != nullptr)
        {
            SendSnapshot(ms_aPendingSnapshots[i]);
        }
    }
}

bool CanSend()
{
    // nothing before the first server-time answer; what is due stays due
    return CNetwork::m_bAuthenticated && IsServerClockSynced();
}

// once per script frame: what changed since the last frame
void ProcessHost(uint32_t now)
{
    if (!CanSend())
    {
        return;
    }
    SendPendingSnapshots();

    CBatch batch(playerid_t{}, false);
    for (int i = 0; i < ms_owned.Count();)
    {
        SOwnedBlip& blip = ms_owned.At(i);
        const int index = CRadar::GetActualBlipArrayIndex(static_cast<int>(blip.handle));
        if (index < 0 || blip.bDisowned)
        {
            // removed (remove_blip, the mission's clean-up, its entity destroyed) or disowned
            if (blip.bSent)
            {
                Packets::Mirror::SMirrorBlipEntry& entry = batch.Next();
                entry.hostHandle = blip.handle;
                entry.op = MirrorBlipsWire::OP_REMOVED;
                ++ms_counters.sentRemoved;
            }
            LogLine("removed handle=%08X sent=%d", blip.handle, blip.bSent);
            ms_owned.RemoveAt(i);
            continue;
        }

        SBlipState state;
        if (!CaptureBlip(index, state))
        {
            ++ms_counters.captureFailures;
            i++;
            continue;
        }
        switch (MirrorBlipsPolicy::DecideSend(blip.bSent, blip.sent, state, now - blip.positionSentAt))
        {
            case eSend::FULL:
                FillFull(batch.Next(), blip.handle, state);
                blip.bSent = true;
                blip.sent = state;
                blip.positionSentAt = now;
                ++ms_counters.sentFull;
                break;
            case eSend::POSITION:
            {
                Packets::Mirror::SMirrorBlipEntry& entry = batch.Next();
                entry.hostHandle = blip.handle;
                entry.op = MirrorBlipsWire::OP_POSITION;
                entry.position = CVector(state.x, state.y, state.z);
                blip.sent.x = state.x;
                blip.sent.y = state.y;
                blip.sent.z = state.z;
                blip.positionSentAt = now;
                ++ms_counters.sentPosition;
                break;
            }
            default:
                break;
        }
        i++;
    }
    batch.Flush(false);
}

void ForgetOwned(const char* why)
{
    if (ms_owned.Count() > 0)
    {
        LogLine("forget-owned why=%s", why);
        if (CLocalPlayer::m_bIsHost)
        {
            // the coordinate blips that are no longer owned go the static way again (M-D1)
            CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
        }
    }
    ms_owned.Clear();
    ms_nPendingSnapshots = 0;
    ms_bBroadcastPending = false;
}

// This game stops being the authority while still connected (a coop mission of another player, a mission it plays
// alone): the others drop what they mirror from it (an empty reset; the server relays it while it still takes this game
// for the authority, else the others notice the authority change themselves).
void RetractOwned(const char* why)
{
    bool bSentAny = false;
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        bSentAny = bSentAny || ms_owned.At(i).bSent;
    }
    if (bSentAny && CanSend())
    {
        CBatch batch(playerid_t{}, true);
        batch.Flush(true);
        ++ms_counters.broadcasts;
    }
    ForgetOwned(why);
}

// ---- the other games ----

struct SResolved
{
    bool bResolved = false;     // this game has the entity
    bool bLocalPlayer = false;  // it is this game's own player
    int entityRef = 0;          // its pool reference (CPools::GetPedRef / GetVehicleRef)
};

bool IsLivePed(CPed* pPed)
{
    return pPed != nullptr && CPools::ms_pPedPool->IsObjectValid(pPed) && pPed->IsVTableValid();
}

// the authority's entity in this game, by membership in the lists
SResolved ResolveEntity(const SBlipState& host)
{
    SResolved resolved;
    switch (host.entityKind)
    {
        case eEntityKind::OBJECT:
            if (CConfigLoader::ms_bCoopHomeInvasion)
                if (CObject* object = CObjectSync::Resolve(static_cast<uint32_t>(host.entityId)))
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetObjectRef(object);
                }
            break;
        case eEntityKind::PLAYER:
        {
            const playerid_t playerId(static_cast<uint32_t>(host.entityId));
            if (playerId == CNetworkPlayerManager::m_localPlayerId)
            {
                resolved.bLocalPlayer = true;
            }
            else if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerId))
            {
                if (IsLivePed(pNetworkPlayer->m_pPed))
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetPedRef(pNetworkPlayer->m_pPed);
                }
            }
            break;
        }
        case eEntityKind::PED:
            if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pedid_t::FromPacked(host.entityId)))
            {
                if (IsLivePed(pNetworkPed->m_pPed))
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetPedRef(pNetworkPed->m_pPed);
                }
            }
            break;
        case eEntityKind::VEHICLE:
            if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicleid_t::FromPacked(host.entityId)))
            {
                // NULL for a train (CNetworkVehicle::CreateVehicle doesn't create one)
                CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
                if (pVehicle != nullptr && CPools::ms_pVehiclePool->IsObjectValid(pVehicle) && pVehicle->IsVTableValid())
                {
                    resolved.bResolved = true;
                    resolved.entityRef = CPools::GetVehicleRef(pVehicle);
                }
            }
            break;
        default:
            break;
    }
    return resolved;
}

CEntryExit* FindEntrance(const SBlipState& host)
{
    const int index = CEntryExitManager::FindNearestEntryExit(
        CVector2D(host.entranceX, host.entranceY), ENTRANCE_SEARCH_RANGE, ENTRANCE_IGNORE_AREA);
    return index >= 0 ? CEntryExitManager::GetEntryExit(index) : nullptr;
}

bool HasLocalTrace(const SMirroredBlip& blip)
{
    return blip.localHandle != -1 && CRadar::GetActualBlipArrayIndex(blip.localHandle) != -1;
}

// a FOLLOW blip's trace at the authority's position
void PlaceFollow(const SMirroredBlip& blip)
{
    if (blip.form == eLocalForm::FOLLOW && HasLocalTrace(blip))
    {
        CRadar::ms_RadarTrace[CRadar::GetActualBlipArrayIndex(blip.localHandle)].m_vecPos =
            CVector(blip.host.x, blip.host.y, blip.host.z);
    }
}

// the authority's fields in this game's trace (CNetworkStaticBlip::Create writes them the same way)
void ApplyFields(SMirroredBlip& blip)
{
    const int index = CRadar::GetActualBlipArrayIndex(blip.localHandle);
    if (index < 0)
    {
        return;
    }
    tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    const SBlipState& host = blip.host;
    trace.m_nColour = host.colour;
    trace.m_nRadarSprite = host.sprite;
    trace.m_nBlipDisplay = MirrorBlipsPolicy::LocalDisplay(host, blip.form);
    trace.m_nBlipSize = host.size;
    trace.m_bBright = host.bBright;
    trace.m_bShortRange = host.bShortRange;
    trace.m_bFriendly = host.bFriendly;
    trace.m_bBlipRemain = host.bRemain;
    trace.m_bBlipFade = host.bFade;
    trace.m_nCoordBlipAppearance = host.coordAppearance;

    if (blip.form == eLocalForm::FOLLOW)
    {
        trace.m_vecPos = CVector(host.x, host.y, host.z);
        CRadar::SetBlipEntryExit(blip.localHandle, nullptr);
    }
    else if (blip.form == eLocalForm::COORD)
    {
        // an interior's blip shows at its entrance through the same entry-exit as on the authority; without it, at the
        // entrance itself (as the static blip snapshot places such blips)
        CEntryExit* pEntryExit = host.bEntrance ? FindEntrance(host) : nullptr;
        if (host.bEntrance && pEntryExit == nullptr)
        {
            trace.m_vecPos = CVector(host.entranceX, host.entranceY, host.entranceZ);
        }
        else
        {
            trace.m_vecPos = CVector(host.x, host.y, host.z);
        }
        CRadar::SetBlipEntryExit(blip.localHandle, pEntryExit);
    }
}

int CreateLocal(const SMirroredBlip& blip, eLocalForm form, uint8_t type, int entityRef)
{
    const eBlipDisplay display = static_cast<eBlipDisplay>(MirrorBlipsPolicy::LocalDisplay(blip.host, form));
    if (form == eLocalForm::ENTITY)
    {
        return CRadar::SetEntityBlip(static_cast<eBlipType>(type), entityRef, 0, display);
    }
    return CRadar::SetCoordBlip(
        static_cast<eBlipType>(type), CVector(blip.host.x, blip.host.y, blip.host.z), 0, display, nullptr);
}

// guest-car-blip-hide: the local player is inside this vehicle (its blip and marker would sit on top of him)
bool IsLocalPlayerInVehicle(int vehicleRef)
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    return CConfigLoader::ms_bGuestCarBlipHide && pPlayer != nullptr && pPlayer->m_nPedFlags.bInVehicle &&
           pPlayer->m_pVehicle != nullptr && CPools::GetVehicleRef(pPlayer->m_pVehicle) == vehicleRef;
}

// Brings this game's blip in line with the authority's: made again when the form, type or entity changed or its trace
// is gone, the fields written after a rebuild or when the authority's state changed (bApply).
void Reconcile(SMirroredBlip& blip, bool bApply)
{
    const SResolved resolved = ResolveEntity(blip.host);
    eLocalForm form = MirrorBlipsPolicy::DecideLocalForm(blip.host, resolved.bResolved, resolved.bLocalPlayer);
    // High Stakes: the guest has its own route (checkpoint + blip per racer), so the host's coordinate blips are not
    // made; the entity blips (NPC racer cars, the host's car) stay as on any mirrored mission
    if (form == eLocalForm::COORD && CHighStakesRace::OwnMarkers())
        form = eLocalForm::NONE;
    if (form == eLocalForm::ENTITY && blip.host.entityKind == eEntityKind::VEHICLE &&
        IsLocalPlayerInVehicle(resolved.entityRef))
        form = eLocalForm::NONE;
    const bool bHave = HasLocalTrace(blip);

    if (form == eLocalForm::NONE)
    {
        if (bHave)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
        blip.localHandle = -1;
        blip.form = eLocalForm::NONE;
        blip.localType = MirrorBlipsPolicy::Type::NONE;
        return;
    }

    const uint8_t type = MirrorBlipsPolicy::LocalType(blip.host, form);
    if (MirrorBlipsPolicy::NeedsRebuild(
            bHave, blip.form, blip.localType, blip.localEntityRef, form, type, resolved.entityRef))
    {
        const bool bSwitch = blip.form == eLocalForm::FOLLOW && form == eLocalForm::ENTITY;
        if (bHave)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
        blip.localHandle = CreateLocal(blip, form, type, resolved.entityRef);
        if (blip.localHandle == -1)
        {
            blip.form = eLocalForm::NONE;
            blip.localType = MirrorBlipsPolicy::Type::NONE;
            if (ms_counters.createFailures++ == 0)
            {
                LogLine("create-failed handle=%08X form=%s (no free radar trace; retried)", blip.hostHandle,
                    FormName(form));
            }
            return;
        }
        blip.form = form;
        blip.localType = type;
        blip.localEntityRef = form == eLocalForm::ENTITY ? resolved.entityRef : 0;
        ++ms_counters.rebuilds;
        if (bSwitch)
        {
            ++ms_counters.switches;
            LogLine("switch handle=%08X form=entity", blip.hostHandle);
        }
        bApply = true;
    }
    if (bApply)
    {
        ApplyFields(blip);
    }
}

void CountForms()
{
    unsigned int entity = 0;
    unsigned int follow = 0;
    unsigned int coord = 0;
    unsigned int none = 0;
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        switch (ms_mirrored.At(i).form)
        {
            case eLocalForm::ENTITY: entity++; break;
            case eLocalForm::FOLLOW: follow++; break;
            case eLocalForm::COORD: coord++; break;
            default: none++; break;
        }
    }
    ms_counters.formEntity = entity;
    ms_counters.formFollow = follow;
    ms_counters.formCoord = coord;
    ms_counters.formNone = none;
}

void DropMirrored(const char* why)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    LogLine("drop-mirrored why=%s", why);
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        const SMirroredBlip& blip = ms_mirrored.At(i);
        if (blip.localHandle != -1)
        {
            CRadar::ClearBlip(blip.localHandle);
        }
    }
    ms_mirrored.Clear();
    CountForms();
}

// This game restarted (a new game or a load): its radar traces were cleared or replaced by the loaded ones, so the local
// handles mean nothing any more and are forgotten without being cleared (one may now name a loaded blip). The
// authority's state stays: the blips are made again at once.
void ForgetLocalHandles(const char* why)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    LogLine("forget-local why=%s", why);
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        SMirroredBlip& blip = ms_mirrored.At(i);
        blip.localHandle = -1;
        blip.form = eLocalForm::NONE;
        blip.localType = MirrorBlipsPolicy::Type::NONE;
    }
    ms_bReconcileNow = true;
}

void ProcessReceiver(uint32_t now)
{
    if (ms_mirrored.Count() == 0)
    {
        return;
    }
    if (!ms_bReconcileNow && now - ms_nLastReconcile < RECONCILE_INTERVAL_MS)
    {
        return;
    }
    ms_bReconcileNow = false;
    ms_nLastReconcile = now;
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        Reconcile(ms_mirrored.At(i), false);
    }
    CountForms();
}

SBlipState ReadState(const Packets::Mirror::SMirrorBlipEntry& entry)
{
    SBlipState state;
    state.type = entry.type;
    state.entityKind = static_cast<eEntityKind>(entry.entityKind);
    state.entityId = entry.entityId;
    state.x = entry.position.x;
    state.y = entry.position.y;
    state.z = entry.position.z;
    state.colour = entry.colour;
    state.sprite = entry.sprite;
    state.display = entry.display;
    state.size = entry.size;
    state.bBright = entry.bBright;
    state.bShortRange = entry.bShortRange;
    state.bFriendly = entry.bFriendly;
    state.bRemain = entry.bRemain;
    state.bFade = entry.bFade;
    state.coordAppearance = entry.coordAppearance;
    state.bEntrance = entry.bEntrance;
    if (entry.bEntrance)
    {
        state.entranceX = entry.entrance.x;
        state.entranceY = entry.entrance.y;
        state.entranceZ = entry.entrance.z;
    }
    return state;
}

void ApplyEntry(const Packets::Mirror::SMirrorBlipEntry& entry)
{
    switch (entry.op)
    {
        case MirrorBlipsWire::OP_REMOVED:
        {
            int localHandle = -1;
            if (!ms_mirrored.Remove(entry.hostHandle, localHandle))
            {
                ++ms_counters.recvUnknown;
                return;
            }
            if (localHandle != -1)
            {
                CRadar::ClearBlip(localHandle);
            }
            ++ms_counters.recvRemoved;
            return;
        }
        case MirrorBlipsWire::OP_POSITION:
        {
            SMirroredBlip* pBlip = ms_mirrored.Find(entry.hostHandle);
            if (pBlip == nullptr)
            {
                ++ms_counters.recvUnknown;
                return;
            }
            pBlip->host.x = entry.position.x;
            pBlip->host.y = entry.position.y;
            pBlip->host.z = entry.position.z;
            PlaceFollow(*pBlip);
            ++ms_counters.recvPosition;
            return;
        }
        default:
        {
            const bool bNew = ms_mirrored.Find(entry.hostHandle) == nullptr;
            SMirroredBlip* pBlip = ms_mirrored.Upsert(entry.hostHandle, ReadState(entry));
            if (pBlip == nullptr)
            {
                ++ms_counters.createFailures;
                return;
            }
            Reconcile(*pBlip, true);
            if (bNew)
            {
                LogLine("rx-new handle=%08X type=%u kind=%u display=%u form=%s local=%d", entry.hostHandle, entry.type,
                    entry.entityKind, entry.display, FormName(pBlip->form), pBlip->localHandle);
            }
            ++ms_counters.recvFull;
            return;
        }
    }
}

// ---- both ----

// The frame counter starts over with a new game or a load (CTimer::Initialise zeroes CTimer::m_FrameCounter). Checked at
// every entry point, so no local handle of the old radar is used after the restart. On the authority its radar was
// reset, so nothing it owned is there any more and the others drop what they mirror from it (its whole set, empty now,
// with bReset); on the others the local handles are forgotten.
void CheckRestart()
{
    const uint32_t frame = CTimer::m_FrameCounter;
    if (ms_bFrameSeen && frame < ms_nLastFrame)
    {
        ForgetLocalHandles("frame-counter");
        const bool bHadOwned = ms_owned.Count() > 0;
        ms_owned.Clear();
        if (bHadOwned && CCoopMission::IsScriptAuthority())
        {
            ms_bBroadcastPending = true;
        }
        if (CLocalPlayer::m_bIsHost)
        {
            // the loaded coordinate blips are nobody's: they go the static way (M-D1)
            CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
        }
        LogLine("restart frame=%u", frame);
    }
    ms_bFrameSeen = true;
    ms_nLastFrame = frame;
}
}  // namespace

void CMirrorBlips::OnHostMissionEnded()
{
    if (!CConfigLoader::ms_bMirrorBlipsClearOnEnd || !IsEnabled() || CCoopMission::IsScriptAuthority())
        return;
    int cleared = 0;
    for (int i = ms_mirrored.Count() - 1; i >= 0; i--)
    {
        const SMirroredBlip& blip = ms_mirrored.At(i);
        if (blip.host.bRemain)
            continue;
        int localHandle = -1;
        const uint32_t hostHandle = blip.hostHandle;
        ms_mirrored.Remove(hostHandle, localHandle);
        if (localHandle != -1)
            CRadar::ClearBlip(localHandle);
        cleared++;
    }
    if (cleared > 0)
    {
        LogLine("mission-ended cleared=%d", cleared);
        CountForms();
    }
}

bool CMirrorBlips::IsEnabled()
{
    return CConfigLoader::ms_bMirrorBlips;
}

bool CMirrorBlips::IsPerPlayerMissionRunning()
{
    // the original scripts have no per-player logic: every mission goes through the C++ rules
    if (CConfigLoader::ms_bVanillaScripts)
        return false;

    // the scripts in scm/scripts that call Coop.Update/Remove(Char|Car)BlipForNetworkPlayer or
    // Coop.ClearAllEntityBlipsForNetworkPlayer (keep in line with them)
    static const char* const perPlayerScripts[] = {
        "COPCAR", "DRUGS1", "INTRO1", "INTRO2", "SMOKE2", "SWEET1", "SWEET1B", "SWEET2", "SWEET3", "SWEET4", "TWAR7"};

    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (!script->m_bIsMission)
            continue;
        for (const char* name : perPlayerScripts)
        {
            if (_strnicmp(script->m_szName, name, 8) == 0)
                return true;
        }
    }
    return false;
}

bool CMirrorBlips::MutesCoopBlipCommands()
{
    return IsEnabled() && !IsPerPlayerMissionRunning();
}

void CMirrorBlips::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    if (!IsEnabled() || !MirrorBlipsPolicy::IsCreatingCommand(opcode) || COpCodeSync::bProcessingNetworkOpcode ||
        !CMissionMirror::IsFamilyScript(pScript) || IsPerPlayerMissionRunning())
    {
        return;
    }
    CheckRestart();

    const uint32_t handle = static_cast<uint32_t>(ScriptParams[0]);
    if (CRadar::GetActualBlipArrayIndex(static_cast<int>(handle)) < 0 || !ms_owned.Add(handle))
    {
        if (!ms_owned.Contains(handle))
        {
            ++ms_counters.ownFailures;
            LogLine("own-failed handle=%08X opcode=%04X script=%.8s", handle, opcode, pScript->m_szName);
        }
        return;
    }
    ++ms_counters.ownedTotal;
    LogLine("own handle=%08X opcode=%04X script=%.8s", handle, opcode, pScript->m_szName);
}

void CMirrorBlips::Process()
{
    const bool ownRoute = CHighStakesRace::OwnMarkers();
    if (ownRoute && !CConfigLoader::ms_bHighStakesGuestBlips) { DropMirrored("high-stakes-own-route"); return; }
    if (!IsEnabled())
    {
        return;
    }
    CheckRestart();

    const uint32_t now = GetTickCount();
    if (!CNetwork::m_bAuthenticated)
    {
        // the session ended
        DropMirrored("disconnected");
        ForgetOwned("disconnected");
        ms_mirrorSenderId = playerid_t{};
    }
    else if (CCoopMission::IsScriptAuthority())
    {
        DropMirrored("this-game-is-authority");
        ms_mirrorSenderId = playerid_t{};
        ProcessHost(now);
    }
    else
    {
        if (ms_owned.Count() > 0)
        {
            RetractOwned("not-authority");
        }
        ms_nPendingSnapshots = 0;
        ms_bBroadcastPending = false;

        // a new authority starts empty (design 3.11); a player out of the mission shows nothing of it
        if (ms_mirrored.Count() > 0 && GetAuthorityId() != ms_mirrorSenderId)
        {
            DropMirrored("authority-changed");
        }
        if (IsReceiverOut())
        {
            DropMirrored("individual-or-left");
        }
        ProcessReceiver(now);
    }
    ms_counters.owned = static_cast<unsigned int>(ms_owned.Count());
    ms_counters.mirrored = static_cast<unsigned int>(ms_mirrored.Count());
}

void CMirrorBlips::HandlePacket(const Packets::Mirror::MirrorBlips& packet)
{
    if (CHighStakesRace::OwnMarkers() && !CConfigLoader::ms_bHighStakesGuestBlips) return;
    if (!IsEnabled())
    {
        return;
    }
    CheckRestart();
    ++ms_counters.recvPackets;

    const playerid_t sender = packet.playerid.value;
    const bool bAuthority = CCoopMission::IsScriptAuthority();
    if (IsReceiverOut() || !MirrorBlipsPolicy::AcceptFrom(bAuthority, GetAuthorityId().value(), sender.value()))
    {
        if (ms_counters.recvDropped++ < 16)
        {
            LogLine("drop-packet sender=%u reset=%d count=%d (not from the current authority, or out)", sender.value(),
                packet.bReset, packet.count);
        }
        return;
    }

    if (sender != ms_mirrorSenderId)
    {
        DropMirrored("new-sender");
        ms_mirrorSenderId = sender;
    }
    if (packet.bReset)
    {
        DropMirrored("reset");
        ++ms_counters.recvResets;
        LogLine("reset sender=%u target=%u count=%d", sender.value(), packet.targetPlayerId.value(), packet.count);
    }
    for (int i = 0; i < packet.count && i < MirrorBlipsWire::MAX_ENTRIES; i++)
    {
        ApplyEntry(packet.entries[i]);
    }
    CountForms();
    ms_counters.mirrored = static_cast<unsigned int>(ms_mirrored.Count());
}

void CMirrorBlips::Disown(uint32_t handle)
{
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        SOwnedBlip& blip = ms_owned.At(i);
        if (blip.handle != handle)
        {
            continue;
        }
        LogLine("disown handle=%08X sent=%d", handle, blip.bSent);
        if (blip.bSent)
        {
            blip.bDisowned = true;  // ProcessHost sends its removal
        }
        else
        {
            ms_owned.RemoveAt(i);
        }
        return;
    }
}

void CMirrorBlips::OnPlayerJoined(playerid_t playerId)
{
    if (!IsEnabled() || !playerId.valid() || !CCoopMission::IsScriptAuthority())
    {
        return;
    }
    for (int i = 0; i < ms_nPendingSnapshots; i++)
    {
        if (ms_aPendingSnapshots[i] == playerId)
        {
            return;
        }
    }
    if (ms_nPendingSnapshots < MAX_PENDING_SNAPSHOTS)
    {
        // sent from the next script frame (ProcessHost), once the server clock is known
        ms_aPendingSnapshots[ms_nPendingSnapshots++] = playerId;
    }
}

bool CMirrorBlips::IsOwnedTrace(int traceIndex)
{
    if (!IsEnabled())
    {
        return false;
    }
    CheckRestart();
    for (int i = 0; i < ms_owned.Count(); i++)
    {
        if (CRadar::GetActualBlipArrayIndex(static_cast<int>(ms_owned.At(i).handle)) == traceIndex)
        {
            ++ms_counters.staticSkipped;
            return true;
        }
    }
    return false;
}

bool CMirrorBlips::IsMirrorTrace(int traceIndex)
{
    if (!IsEnabled())
    {
        return false;
    }
    CheckRestart();
    for (int i = 0; i < ms_mirrored.Count(); i++)
    {
        const SMirroredBlip& blip = ms_mirrored.At(i);
        if (blip.localHandle != -1 && CRadar::GetActualBlipArrayIndex(blip.localHandle) == traceIndex)
        {
            ++ms_counters.staticKept;
            return true;
        }
    }
    return false;
}

void CMirrorBlips::WriteDiagSummary(FILE* file)
{
    const SCounters& c = ms_counters;
    fprintf(file,
        "  mirror-blips: enabled=%d owned=%u total=%u ownFail=%u sent full=%u pos=%u removed=%u packets=%u snapshots=%u "
        "broadcasts=%u captureFail=%u staticSkipped=%u | recv mirrored=%u entity=%u follow=%u coord=%u none=%u "
        "packets=%u dropped=%u resets=%u full=%u pos=%u removed=%u unknown=%u rebuilds=%u switches=%u createFail=%u "
        "staticKept=%u\n",
        CConfigLoader::ms_bMirrorBlips ? 1 : 0, c.owned, c.ownedTotal, c.ownFailures, c.sentFull, c.sentPosition,
        c.sentRemoved, c.sentPackets, c.snapshots, c.broadcasts, c.captureFailures, c.staticSkipped, c.mirrored,
        c.formEntity, c.formFollow, c.formCoord, c.formNone, c.recvPackets, c.recvDropped, c.recvResets, c.recvFull,
        c.recvPosition, c.recvRemoved, c.recvUnknown, c.rebuilds, c.switches, c.createFailures, c.staticKept);
}
