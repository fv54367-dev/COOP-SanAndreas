#include "stdafx.h"
#include "CCutsceneSkipVote.h"
#include "COpCodeSync.h"
#include "CCutsceneMgr.h"
#include "CCamera.h"
#include <CCoopMission.h>

static std::vector<playerid_t> ms_voters;
static bool ms_bLocalVoted = false;
static bool ms_bSkipNow = false;  // not host: the host skipped the cutscene
static bool ms_bPlayersHidden = false;

static bool IsCutsceneSkipButtonBeingPressed()
{
    return plugin::CallAndReturn<bool, 0x4D5D10>();
}

static int GetPlayerCount()
{
    return (int)CNetworkPlayerManager::m_pPlayers.size() + 1;
}

// a scene of the host's coop mission is on screen
static bool IsNetworkSceneActive()
{
    if (CCoopMission::IsScriptAuthority())
        return COpCodeSync::IsSyncedMissionRunning() && (CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn);

    return CCutsceneSkipVote::ms_bNetworkCutscene ||
           (TheCamera.m_bWideScreenOn && !COpCodeSync::IsLocalMissionRunning());
}

static void AddVoter(playerid_t playerid)
{
    if (std::find(ms_voters.begin(), ms_voters.end(), playerid) != ms_voters.end())
        return;

    ms_voters.push_back(playerid);

    std::string name;
    if (playerid == CNetworkPlayerManager::m_localPlayerId)
        name = CLocalPlayer::m_Name;
    else if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerid))
        name = pNetworkPlayer->GetName();
    else
        name = "?";

    if (GetPlayerCount() > 1)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} %s quiere saltear la escena (%d/%d).", name.c_str(),
            (int)ms_voters.size(), GetPlayerCount());
    }
}

static void ResetVotes()
{
    ms_voters.clear();
    ms_bLocalVoted = false;
}

static void VoteLocally()
{
    if (ms_bLocalVoted)
        return;

    ms_bLocalVoted = true;
    AddVoter(CNetworkPlayerManager::m_localPlayerId);

    Packets::Scripts::SkipCutscene packet{};
    packet.skipNow = false;
    GetPacketFactory().Send(packet);
}

// host: everyone voted, skip for everyone
static bool HostShouldSkip()
{
    // players that left don't count
    ms_voters.erase(std::remove_if(ms_voters.begin(), ms_voters.end(),
                        [](playerid_t id)
                        { return id != CNetworkPlayerManager::m_localPlayerId && !CNetworkPlayerManager::GetPlayer(id); }),
        ms_voters.end());

    if ((int)ms_voters.size() < GetPlayerCount())
        return false;

    Packets::Scripts::SkipCutscene packet{};
    packet.skipNow = true;
    GetPacketFactory().Send(packet);

    ResetVotes();
    return true;
}

static bool ProcessSkipButton(bool bNetworkScene)
{
    bool bPressed = IsCutsceneSkipButtonBeingPressed();

    if (!CNetwork::m_bAuthenticated || !bNetworkScene)
        return bPressed;

    // the host decides: its button skips the scene for everyone
    if (CCoopMission::IsScriptAuthority())
    {
        if (!bPressed)
            return false;

        Packets::Scripts::SkipCutscene packet{};
        packet.skipNow = true;
        GetPacketFactory().Send(packet);
        return true;
    }

    if (ms_bSkipNow)
    {
        ms_bSkipNow = false;
        return true;
    }

    return false;
}

// CCutsceneMgr::Update
static bool CutsceneUpdate_IsSkipButtonPressed_Hook()
{
    bool bNetworkScene =
        CCoopMission::IsScriptAuthority() ? COpCodeSync::IsSyncedMissionRunning() : CCutsceneSkipVote::ms_bNetworkCutscene;
    return ProcessSkipButton(bNetworkScene);
}

// CRunningScript::Process, skip_cutscene_start of the script in ecx
static bool __fastcall ScriptSceneSkip_IsSkipButtonPressed_Hook(CRunningScript* pScript)
{
    bool bNetworkScene = CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(pScript);
    return ProcessSkipButton(bNetworkScene);
}

// is_skip_cutscene_button_pressed command
static bool ScriptCommand_IsSkipButtonPressed_Hook()
{
    bool bNetworkScene = CCoopMission::IsScriptAuthority() && COpCodeSync::IsSyncedMissionRunning();
    return ProcessSkipButton(bNetworkScene);
}

void CCutsceneSkipVote::InjectHooks()
{
    patch::RedirectCall(0x5B1947, CutsceneUpdate_IsSkipButtonPressed_Hook);
    patch::RedirectCall(0x469F0E, ScriptSceneSkip_IsSkipButtonPressed_Hook);
    patch::RedirectCall(0x475459, ScriptCommand_IsSkipButtonPressed_Hook);
}

void CCutsceneSkipVote::HandlePacket(Packets::Scripts::SkipCutscene* pPacket)
{
    if (pPacket->skipNow)
    {
        ResetVotes();
        if (!CCoopMission::IsScriptAuthority() && CCutsceneMgr::ms_running && ms_bNetworkCutscene)
        {
            ms_bSkipNow = true;
        }
        return;
    }

    AddVoter(pPacket->senderPlayerid.value);
}

static void SetNetworkPlayersVisible(bool bVisible)
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_pPed)
        {
            pNetworkPlayer->m_pPed->m_bIsVisible = bVisible;
        }
    }
}

void CCutsceneSkipVote::Process()
{
    // the cutscene has its own CJ, the network players would stand in the middle of it
    if (CCutsceneMgr::ms_running)
    {
        SetNetworkPlayersVisible(false);
        ms_bPlayersHidden = true;
    }
    else if (ms_bPlayersHidden)
    {
        SetNetworkPlayersVisible(true);
        ms_bPlayersHidden = false;
    }

    if (!CCutsceneMgr::ms_running)
    {
        ms_bSkipNow = false;
    }

    if (!IsNetworkSceneActive())
    {
        ResetVotes();
        return;
    }

}
