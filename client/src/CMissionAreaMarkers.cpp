#include "stdafx.h"
#include "CMissionAreaMarkers.h"
#include "CMissionSyncRules.h"
#include "CCoopMission.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"
#include "Commands/CommandHooks.h"

static constexpr uintptr_t HIGHLIGHT_IMPORTANT_AREA = 0x485E00;
static constexpr DWORD SEND_INTERVAL_MS = 200;
static constexpr DWORD KEEP_MS = 600;
static constexpr int MAX_AREAS = 8;

// callers of CTheScripts::HighlightImportantArea in gta_sa.exe 1.0 US, all in script command handlers
static const uintptr_t aCallSites[] = {0x467989, 0x467A96, 0x467FA0, 0x46805E, 0x4801D7, 0x48704F, 0x4873A4, 0x4876B2,
    0x4879B2, 0x487CA9, 0x487F13, 0x488E6A, 0x489111, 0x489378};

struct SArea
{
    uint32_t m_nId;
    DWORD m_nTime; // sent: last send; received: drawn until
    float m_fFromX, m_fFromY, m_fToX, m_fToY, m_fZ;

    bool SameRect(float fromX, float fromY, float toX, float toY, float z) const
    {
        return m_fFromX == fromX && m_fFromY == fromY && m_fToX == toX && m_fToY == toY && m_fZ == z;
    }
};

static SArea aSentAreas[MAX_AREAS]{};
static SArea aReceivedAreas[MAX_AREAS]{};

// the slot of this id, or the one used least recently
static SArea& FindSlot(SArea* areas, uint32_t id)
{
    SArea* oldest = &areas[0];
    for (int i = 0; i < MAX_AREAS; i++)
    {
        if (areas[i].m_nTime && areas[i].m_nId == id)
            return areas[i];
        if (areas[i].m_nTime < oldest->m_nTime)
            oldest = &areas[i];
    }
    return *oldest;
}

static void SendArea(uint32_t id, float fromX, float fromY, float toX, float toY, float z)
{
    if (!CConfigLoader::ms_bNativeMarkers || !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority())
        return;

    // only the areas of the mission the others play with him (not shops, launchers or an individual mission)
    CRunningScript* script = CommandHooks::GetCurrentScript();
    if (!script || !script->m_bIsMission || !COpCodeSync::IsScriptSynced(script))
        return;

    const DWORD now = GetTickCount();
    SArea& slot = FindSlot(aSentAreas, id);
    if (slot.m_nTime && slot.m_nId == id && slot.SameRect(fromX, fromY, toX, toY, z) && now - slot.m_nTime < SEND_INTERVAL_MS)
        return;

    slot = {id, now, fromX, fromY, toX, toY, z};

    Packets::Blips::MissionAreaMarker packet{};
    packet.id = id;
    packet.fromX = fromX;
    packet.fromY = fromY;
    packet.toX = toX;
    packet.toY = toY;
    packet.z = z;
    GetPacketFactory().Send(packet);
}

static void __cdecl HighlightImportantArea_Hook(uint32_t id, float fromX, float fromY, float toX, float toY, float z)
{
    CRunningScript* script = CommandHooks::GetCurrentScript();
    if (CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        script && _strnicmp(script->m_szName, "SWEET4", 8) == 0)
    {
        static const CVector aPaintShopAreas[] = {{2075.55f, -1831.09f, 12.21f}, {1024.55f, -1039.09f, 35.21f}};
        for (const CVector& center : aPaintShopAreas)
        {
            // Hide only the large paint-shop checks; keep the LOCATE result unchanged.
            if (fabsf((fromX + toX) * 0.5f - center.x) <= 0.1f &&
                fabsf((fromY + toY) * 0.5f - center.y) <= 0.1f && fabsf(z - center.z) <= 0.1f &&
                fabsf(fabsf(toX - fromX) * 0.5f - 20.0f) <= 0.1f &&
                fabsf(fabsf(toY - fromY) * 0.5f - 20.0f) <= 0.1f)
                return;
        }
    }

    plugin::Call<HIGHLIGHT_IMPORTANT_AREA>(id, fromX, fromY, toX, toY, z);
    SendArea(id, fromX, fromY, toX, toY, z);
}

void CMissionAreaMarkers::InjectHooks()
{
    for (uintptr_t site : aCallSites)
    {
        // only a call that still goes to the original function is redirected (another plugin may own it)
        const uint8_t* code = reinterpret_cast<const uint8_t*>(site);
        const uintptr_t target = site + 5 + *reinterpret_cast<const int32_t*>(code + 1);
        if (code[0] == 0xE8 && target == HIGHLIGHT_IMPORTANT_AREA)
            patch::RedirectCall(site, HighlightImportantArea_Hook);
    }
}

void CMissionAreaMarkers::Handle(Packets::Blips::MissionAreaMarker* packet)
{
    // the authority draws its own; a player that plays alone or left the mission does not see the others' areas
    if (!CConfigLoader::ms_bNativeMarkers || CCoopMission::IsScriptAuthority() || CMissionMode::IsIndividual() ||
        CCoopMission::HasLocalLeft())
        return;

    SArea& slot = FindSlot(aReceivedAreas, packet->id);
    if (!slot.m_nTime || slot.m_nId != packet->id)
    {
        CMissionMirror::LogEventf("rx-area id=%08X from=%.1f,%.1f to=%.1f,%.1f z=%.1f", packet->id, packet->fromX,
            packet->fromY, packet->toX, packet->toY, packet->z);
    }
    slot = {packet->id, GetTickCount() + KEEP_MS, packet->fromX, packet->fromY, packet->toX, packet->toY, packet->z};
}

void CMissionAreaMarkers::Process()
{
    if (CCoopMission::IsScriptAuthority())
        return;

    const DWORD now = GetTickCount();
    for (SArea& area : aReceivedAreas)
    {
        if (!area.m_nTime)
            continue;
        if (static_cast<int>(area.m_nTime - now) < 0)
        {
            area.m_nTime = 0;
            continue;
        }
        plugin::Call<HIGHLIGHT_IMPORTANT_AREA>(area.m_nId, area.m_fFromX, area.m_fFromY, area.m_fToX, area.m_fToY, area.m_fZ);
    }
}

void CMissionAreaMarkers::Clear()
{
    for (SArea& area : aReceivedAreas)
        area.m_nTime = 0;
    for (SArea& area : aSentAreas)
        area.m_nTime = 0;
}

bool CMissionAreaMarkers::IsDrawnNear(const CVector& position)
{
    const DWORD now = GetTickCount();
    for (const SArea& area : aReceivedAreas)
    {
        if (!area.m_nTime || static_cast<int>(area.m_nTime - now) < 0)
            continue;
        const float dx = (area.m_fFromX + area.m_fToX) * 0.5f - position.x;
        const float dy = (area.m_fFromY + area.m_fToY) * 0.5f - position.y;
        if (dx * dx + dy * dy < 3.0f * 3.0f)
            return true;
    }
    return false;
}
