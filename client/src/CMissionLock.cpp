#include "stdafx.h"
#include "CMissionLock.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include <network/packets/scripts.h>
#include <mission_ids.h>

bool CMissionLock::IsIndependentIntro(int missionId)
{
    return CConfigLoader::ms_bIntroIndependent && missionId == 2;
}

bool CMissionLock::IsLocalIntroRunning()
{
    if (!CConfigLoader::ms_bIntroIndependent)
        return false;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && (CMissionMirror::GetRunningMissionId() == 2 ||
            _strnicmp(script->m_szName, "INTRO", 8) == 0))
            return true;
    }
    return false;
}

// ends a mission script that has just been launched, as terminate_this_script would, and gives back what its launcher took
void CMissionLock::AbortMission(CRunningScript* pScript)
{
    if (pScript == nullptr)
        return;

    if (pScript->m_bIsMission)
        CTheScripts::bAlreadyRunningAMissionScript = false;

    CTheScripts::MissionCleanUp.Process();
    // the launcher may have faded the screen out for the mission
    TheCamera.Fade(0.0f, 1);

    pScript->RemoveScriptFromList(&CTheScripts::pActiveScripts);
    pScript->AddScriptToList(&CTheScripts::pIdleScripts);
    pScript->m_bIsActive = false;

    // OnAMissionFlag, pads, fade and widescreen back to normal
    CCoopMission::LocalCleanup();
}

void CMissionLock::ShowBlocked(playerid_t ownerId)
{
    // the marker is asked every frame while the player stands in it: one message every 5 seconds
    const uint32_t now = GetTickCount();
    if (now - ms_nLastMessageTick < 5000)
        return;
    ms_nLastMessageTick = now;

    std::string who = "otro jugador";
    if (CNetworkPlayer* pOwner = CNetworkPlayerManager::GetPlayer(ownerId))
        who = pOwner->GetName();

    CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esa mision ya la esta jugando " + who + ". Podes hacer otras.");
    // a message that stays 5 seconds with who plays it
    static char szMessage[128];
    snprintf(szMessage, sizeof(szMessage), "%s esta jugando esa mision. Espera a que la termine o fracase.", who.c_str());
    CMessages::AddMessageJumpQ(szMessage, 5000, 0, false);
}

void CMissionLock::RememberBlockedLauncher(CRunningScript* pLauncher, int missionId)
{
    if (pLauncher == nullptr)
        return;

    strncpy_s(ms_szBlockedLauncher, pLauncher->m_szName, 7);
    ms_nBlockedMission = missionId;
}

bool CMissionLock::IsLaunchBlocked(CRunningScript* pLauncher, int missionId)
{
    if (pLauncher == nullptr || !CNetwork::m_bAuthenticated)
        return false;

    if (missionId < 0 && ms_nBlockedMission >= 0 && strncmp(ms_szBlockedLauncher, pLauncher->m_szName, 7) == 0)
        missionId = ms_nBlockedMission;

    if (missionId < 0 || missionId >= MAX_MISSIONS || IsSideMissionId(missionId) || IsIndependentIntro(missionId))
        return false;

    const playerid_t owner = ms_owner[missionId];
    if (owner.valid() && owner != CNetworkPlayerManager::m_localPlayerId)
    {
        ShowBlocked(owner);
        return true;
    }

    // the lock is free again: the launcher can try
    if (missionId == ms_nBlockedMission)
        ms_nBlockedMission = -1;

    return false;
}

bool CMissionLock::OnMissionStarted(int missionId, CRunningScript* pScript)
{
    // side missions are never locked: several players can play the same one
    if (!CNetwork::m_bAuthenticated || missionId < 0 || missionId >= MAX_MISSIONS ||
        IsSideMissionId(missionId) || IsIndependentIntro(missionId))
        return false;

    const playerid_t owner = ms_owner[missionId];
    if (owner.valid() && owner != CNetworkPlayerManager::m_localPlayerId)
    {
        AbortMission(pScript);
        ShowBlocked(owner);
        return true;
    }

    ms_owner[missionId] = CNetworkPlayerManager::m_localPlayerId;
    ms_nMyMission = missionId;
    ms_pMyScript = pScript;

    Packets::Scripts::MissionLockRequest packet{};
    packet.missionId = missionId;
    GetPacketFactory().Send(packet);
    return false;
}

void CMissionLock::OnMissionEnded()
{
    if (ms_nMyMission < 0)
        return;

    if (ms_owner[ms_nMyMission] == CNetworkPlayerManager::m_localPlayerId)
        ms_owner[ms_nMyMission] = playerid_t{};

    if (CNetwork::m_bAuthenticated)
    {
        Packets::Scripts::MissionLockRelease packet{};
        packet.missionId = ms_nMyMission;
        GetPacketFactory().Send(packet);
    }

    ms_nMyMission = -1;
    ms_pMyScript = nullptr;
}

void CMissionLock::HandleState(int missionId, bool locked, playerid_t ownerId)
{
    if (missionId < 0 || missionId >= MAX_MISSIONS || IsIndependentIntro(missionId))
        return;

    const playerid_t newOwner = locked ? ownerId : playerid_t{};

    // two players started the same mission at once and the server gave it to the other: this one ends
    if (missionId == ms_nMyMission && newOwner.valid() && newOwner != CNetworkPlayerManager::m_localPlayerId)
    {
        CRunningScript* pScript = ms_pMyScript;
        ms_nMyMission = -1;
        ms_pMyScript = nullptr;
        ms_owner[missionId] = newOwner;

        for (CRunningScript* pActive = CTheScripts::pActiveScripts; pActive != nullptr; pActive = pActive->m_pNext)
        {
            if (pActive == pScript)
            {
                AbortMission(pScript);
                ShowBlocked(newOwner);
                break;
            }
        }
        return;
    }

    ms_owner[missionId] = newOwner;
}

void CMissionLock::Reset()
{
    for (auto& owner : ms_owner)
        owner = playerid_t{};
    ms_nMyMission = -1;
    ms_pMyScript = nullptr;
}
