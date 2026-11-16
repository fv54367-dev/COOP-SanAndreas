#include "stdafx.h"
#include "DamageHooks.h"

#include "CCoopMission.h"
#include "CMissionSyncRules.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CPedGroups.h"
#include "CPedDamageSync.h"
#include "mission_ids.h"

// gta-reversed Event.h: GetEventType and GetSourceEntity are vtable slots 1 and 10.
bool DamageHooks::ShouldDiscardAllyEvent(void* event, CPed* ped)
{
    if (!event || !ped || !CConfigLoader::ms_bMissionAllyNoGuestDamage || !CMissionSyncRules::IsEnabled() ||
        !CNetwork::m_bAuthenticated || COpCodeSync::bProcessingNetworkOpcode ||
        CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft() ||
        ped->m_nCreatedBy != 2 || ped->IsPlayer() || CNetworkPlayerManager::GetPlayer(ped) ||
        !CLocalPlayer::GetIsHostingEntity(ped))
        return false;

    auto** vtable = *reinterpret_cast<void***>(event);
    const int type = reinterpret_cast<int(__thiscall*)(void*)>(vtable[1])(event);
    if (type != 9 && type != 31)
        return false;

    bool storyRunning = false;
    if (CCoopMission::IsScriptAuthority())
    {
        for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && CMissionSyncRules::IsStoryScript(script))
                storyRunning = true;
    }
    else
    {
        // main.txt DEFINE MISSION: story 11..112, excluding schools, races and Cesar.
        for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
        {
            const bool authority = CCoopMission::IsRunning() ? player->m_playerId == CCoopMission::GetOwnerId() : player->m_bIsHost;
            if (authority && CCoopMission::ShouldIncludeNetworkPlayer(player) && player->m_nRunningMission >= 11 &&
                player->m_nRunningMission <= 112 && !IsSideMissionId(player->m_nRunningMission) &&
                !IsIndividualOnlyMissionId(player->m_nRunningMission, CConfigLoader::ms_bCoopLowrider && CConfigLoader::ms_bVanillaScripts))
                storyRunning = true;
        }
    }
    if (!storyRunning)
        return false;

    CEntity* source = reinterpret_cast<CEntity*(__thiscall*)(void*)>(vtable[10])(event);
    if (!source)
        return false;
    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(source);
    if (player)
    {
        const bool authority = CCoopMission::IsRunning() ? player->m_playerId == CCoopMission::GetOwnerId() : player->m_bIsHost;
        if (authority || !CCoopMission::ShouldIncludeNetworkPlayer(player))
            return false;
    }
    else if (source != FindPlayerPed(0) || CCoopMission::IsScriptAuthority())
        return false;

    const auto& acquaintance = ped->m_acquaintance;
    CPed* localPlayer = FindPlayerPed(0);
    bool inPlayerGroup = localPlayer && CPedGroups::AreInSameGroup(ped, localPlayer);
    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
        if (networkPlayer->m_pPed && CPedGroups::AreInSameGroup(ped, networkPlayer->m_pPed))
            inPlayerGroup = true;
    if (!inPlayerGroup && (((acquaintance.m_nRespect | acquaintance.m_nLike) & 1) == 0 ||
        ((acquaintance.m_nDislike | acquaintance.m_nHate) & 1) != 0))
        return false;

    static DWORD lastLog = 0;
    const DWORD now = GetTickCount();
    if (!lastLog || now - lastLog >= 5000)
    {
        lastLog = now;
        CMissionMirror::LogEventf("rule-ally-protected event=%d model=%d", type, ped->m_nModelIndex);
    }
    return true;
}

bool __fastcall CEventDamage__AffectsPed_Hook(CEventDamage* This, SKIP_EDX, CPed* pPed)
{
    // gta-reversed CEventDamage::AffectsPed (0x4B35A0): checked before damage calculation.
    if (DamageHooks::ShouldDiscardAllyEvent(This, pPed))
        return false;
    // ped-damage-authority: the syncer of an NPC decides its damage and its hits on players
    if (CPedDamageSync::ShouldDiscard(This, pPed))
        return false;
    return plugin::CallMethodAndReturn<bool, 0x4B35A0>(This, pPed);
}

void DamageHooks::InjectHooks()
{
    patch::RedirectCall(0x439055, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x4813CD, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x4BA06F, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x5680B8, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x568271, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x5E2F57, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x5E3020, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x5F0CBB, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x5F0F25, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6049B1, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x604AA0, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6297FB, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x65401F, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x656974, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x67C8FC, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x686033, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x68B44E, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6A929D, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6A94DF, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6B6378, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6B6566, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6D83F5, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6DB30B, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x6DBE7E, CEventDamage__AffectsPed_Hook);
    patch::RedirectCall(0x73A687, CEventDamage__AffectsPed_Hook);
    patch::SetPointer(0x85AD8C, CEventDamage__AffectsPed_Hook);
}