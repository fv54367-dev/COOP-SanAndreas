#include "CHighStakesRace.h"
#include "stdafx.h"
#include "CMirrorRaceCheckpoints.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"

using Packets::Blips::MissionRaceCheckpoints;

static constexpr uintptr_t SCRIPT_CHECKPOINT_ARRAY = 0xA44070;
static constexpr DWORD SEND_INTERVAL_MS = 250;
static constexpr DWORD RESEND_MS = 2000;
static constexpr DWORD KEEP_MS = 3000;

struct SScriptCheckpointSlot
{
    bool m_bUsed;
    char m_pad;
    int16_t m_nId;
    uint8_t* m_pCheckpoint;
};
static_assert(sizeof(SScriptCheckpointSlot) == 8, "tScriptCheckpoint is 8 bytes");

static MissionRaceCheckpoints lastSent{};
static DWORD nLastSend = 0;

static int aLocalHandles[MissionRaceCheckpoints::MAX_CHECKPOINTS]{};
static MissionRaceCheckpoints::Entry aLocalEntries[MissionRaceCheckpoints::MAX_CHECKPOINTS]{};
static DWORD nLastReceived = 0;

static bool IsSyncedMissionRunning()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && COpCodeSync::IsScriptSynced(script))
            return true;
    }
    return false;
}

static bool SameEntry(const MissionRaceCheckpoints::Entry& a, const MissionRaceCheckpoints::Entry& b)
{
    return a.used == b.used && a.type == b.type && memcmp(&a.pos, &b.pos, sizeof(CVector)) == 0 &&
           memcmp(&a.dir, &b.dir, sizeof(CVector)) == 0 && a.size == b.size;
}

static void DeleteLocal(int slot)
{
    if (aLocalHandles[slot] != 0)
        Command<Commands::DELETE_CHECKPOINT>(aLocalHandles[slot]);
    aLocalHandles[slot] = 0;
    aLocalEntries[slot] = {};
}

void CMirrorRaceCheckpoints::Process()
{
    if (!CConfigLoader::ms_bVanillaScripts || !CNetwork::m_bConnected)
        return;

    if (!CCoopMission::IsScriptAuthority())
    {
        // the authority stopped sending (its mission ended or it left): the checkpoints go
        if (nLastReceived && GetTickCount() - nLastReceived > KEEP_MS)
            Clear();
        return;
    }

    const DWORD now = GetTickCount();
    if (now - nLastSend < SEND_INTERVAL_MS || !IsSyncedMissionRunning())
        return;

    MissionRaceCheckpoints packet{};
    const auto* slots = reinterpret_cast<const SScriptCheckpointSlot*>(SCRIPT_CHECKPOINT_ARRAY);
    for (int i = 0; i < MissionRaceCheckpoints::MAX_CHECKPOINTS; i++)
    {
        MissionRaceCheckpoints::Entry& entry = packet.entries[i];
        const uint8_t* cp = slots[i].m_pCheckpoint;
        if (!slots[i].m_bUsed || cp == nullptr)
            continue;
        entry.used = true;
        entry.type = static_cast<uint8_t>(*reinterpret_cast<const uint16_t*>(cp));
        entry.pos = *reinterpret_cast<const CVector*>(cp + 0x10);
        entry.dir = *reinterpret_cast<const CVector*>(cp + 0x1C);
        entry.size = *reinterpret_cast<const float*>(cp + 0x2C);
    }

    bool bChanged = false;
    for (int i = 0; i < MissionRaceCheckpoints::MAX_CHECKPOINTS; i++)
        bChanged |= !SameEntry(packet.entries[i], lastSent.entries[i]);
    if (!bChanged && now - nLastSend < RESEND_MS)
        return;

    nLastSend = now;
    lastSent = packet;
    GetPacketFactory().Send(packet);
}

void CMirrorRaceCheckpoints::Handle(MissionRaceCheckpoints* packet)
{
    if (CHighStakesRace::OwnMarkers()) { Clear(); return; }
    if (!CConfigLoader::ms_bVanillaScripts || CCoopMission::IsScriptAuthority() || CMissionMode::IsIndividual() ||
        CCoopMission::HasLocalLeft())
        return;

    nLastReceived = GetTickCount();
    for (int i = 0; i < MissionRaceCheckpoints::MAX_CHECKPOINTS; i++)
    {
        const MissionRaceCheckpoints::Entry& entry = packet->entries[i];
        if (SameEntry(entry, aLocalEntries[i]))
            continue;

        DeleteLocal(i);
        if (!entry.used || entry.type > 8)
            continue;

        int handle = 0;
        Command<Commands::CREATE_CHECKPOINT>((int)entry.type, entry.pos.x, entry.pos.y, entry.pos.z, entry.pos.x + entry.dir.x,
            entry.pos.y + entry.dir.y, entry.pos.z + entry.dir.z, entry.size, &handle);
        aLocalHandles[i] = handle;
        aLocalEntries[i] = entry;
        CMissionMirror::LogEventf("rx-race-checkpoint slot=%d type=%d pos=%.1f,%.1f,%.1f", i, entry.type, entry.pos.x,
            entry.pos.y, entry.pos.z);
    }
}

void CMirrorRaceCheckpoints::Clear()
{
    for (int i = 0; i < MissionRaceCheckpoints::MAX_CHECKPOINTS; i++)
        DeleteLocal(i);
    nLastReceived = 0;
}
