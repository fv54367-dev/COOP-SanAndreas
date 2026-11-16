#include "stdafx.h"
#include "CMissionLock.h"
#include "CMissionHints.h"
#include "CCoopMission.h"

#include "COpCodeSync.h"
#include "CTestMode.h"
#include "CMissionMode.h"
#include "CMissionMirror.h"
#include "CVsLobby.h"
#include <mission_ids.h>
#include "UI/CChat.h"
#include "UI/CCheatMenu.h"
#include "network/packets/scripts.h"
#include <CNetworkCheckpoint.h>
#include <CMissionAreaMarkers.h>
#include <CMirrorRaceCheckpoints.h>
#include <CNetworkEntityBlip.h>

#include "imgui.h"

#include <algorithm>
#include <string>

namespace
{
constexpr uint32_t MIN_INVITE_MS = 2000;

void CopyText(char* szDst, size_t nDstSize, const char* szSrc)
{
    if (nDstSize == 0)
        return;

    if (szSrc == nullptr)
    {
        szDst[0] = '\0';
        return;
    }

    const size_t nLen = strnlen(szSrc, nDstSize - 1);
    memcpy(szDst, szSrc, nLen);
    szDst[nLen] = '\0';
}

std::string PlayerName(playerid_t playerid)
{
    if (playerid == CNetworkPlayerManager::m_localPlayerId)
        return CLocalPlayer::m_Name[0] != '\0' ? CLocalPlayer::m_Name : "Vos";

    if (CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerid))
    {
        std::string name = pNetworkPlayer->GetName();
        if (!name.empty())
            return name;
    }

    return "Jugador " + std::to_string(playerid.value());
}

bool IsMenuOrChatOpen()
{
    return FrontEndMenuManager.m_bMenuActive || CCheatMenu::ms_bOpen || CModeMenu::ms_bOpen || CChat::m_bInputActive;
}

bool IsLocalPlayerDeadOrArrested()
{
    return CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
           CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED;
}

bool IsScriptRunning(CRunningScript* pScript)
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script == pScript)
            return true;
    }
    return false;
}

// same as Coop.EnableSyncingThisScript (0x1D12): the coop owner's mission starts being sent as OPCODE_SYNC
void EnableScriptSyncing(CRunningScript* pScript)
{
    if (pScript == nullptr)
        return;

    // F bug 3: the table never empties, so more than 256 different mission names in the same session would write past
    // the array (the assert compiles out in release); the mission simply stays unsynced
    if (COpCodeSync::ms_iFreeSyncedScript >= ARRAY_SIZE(COpCodeSync::ms_aszSyncedScripts))
        return;

    for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
    {
        if (strnicmp(COpCodeSync::ms_aszSyncedScripts[i], pScript->m_szName, 7) == 0)
            return;
    }

    strncpy_s(COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript], 8, pScript->m_szName, 7);
    ++COpCodeSync::ms_iFreeSyncedScript;
}
}  // namespace

bool CCoopMission::IsActive()
{
    return CConfigLoader::ms_bCoopSideMissions && ms_ePhase != PHASE_FREE;
}

bool CCoopMission::IsRunning()
{
    return CConfigLoader::ms_bCoopSideMissions && ms_ePhase == PHASE_RUNNING;
}

bool CCoopMission::IsOwner()
{
    return IsActive() && ms_ownerId == CNetworkPlayerManager::m_localPlayerId;
}

bool CCoopMission::IsLocalParticipant()
{
    if (ms_bLocalLeft)
        return false;

    return IsParticipant(CNetworkPlayerManager::m_localPlayerId);
}

bool CCoopMission::IsParticipant(playerid_t playerid)
{
    if (!IsActive() || !playerid.valid())
        return false;

    for (int i = 0; i < ms_nParticipantCount; i++)
    {
        if (ms_participants[i] == playerid)
            return true;
    }
    return false;
}

playerid_t CCoopMission::GetOwnerId()
{
    return ms_ownerId;
}

const char* CCoopMission::GetTitle()
{
    return ms_szTitle;
}

bool CCoopMission::IsScriptAuthority()
{
    if (CMissionLock::IsLocalIntroRunning())
        return false;

    // while the coop mission runs its owner runs the script; during the 12 s offer (nothing is synced yet) and with no
    // mission at all the host keeps it, so a story mission in flight is never interrupted
    if (IsRunning())
        return ms_ownerId == CNetworkPlayerManager::m_localPlayerId;

    // a host that plays alone does not send his mission to the others (nor their cutscenes and camera)
    return CLocalPlayer::m_bIsHost && !IsLocalIndividualMission();
}

bool CCoopMission::IsLocalIndividualMission()
{
    if (CMissionLock::IsLocalIntroRunning())
        return true;

    if (IsRunning())
        return false;

    // IsPlayingAlone: he chose individual, or every other player did (then nobody is part of his mission)
    return CMissionMode::IsPlayingAlone() || IsIndividualOnlyMissionId(CMissionMirror::GetRunningMissionId(), CConfigLoader::ms_bCoopLowrider && CConfigLoader::ms_bVanillaScripts);
}

bool CCoopMission::ShouldIncludeNetworkPlayer(const CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr || pNetworkPlayer->m_bLateForMission || pNetworkPlayer->m_bOtherScripts)
        return false;

    // outside a running coop mission every network player is part of the mission, as before, except the ones that play
    // alone, and all of them when the local player does
    if (!IsRunning())
        return !IsLocalIndividualMission() && !pNetworkPlayer->m_bIndividualMode;

    return IsParticipant(pNetworkPlayer->m_playerId);
}

bool CCoopMission::CanOfferCoop()
{
    if (!CNetwork::m_bAuthenticated)
        return false;

    // nobody else to invite: the mission is played alone
    if (CNetworkPlayerManager::m_pPlayers.empty())
        return false;

    if (IsActive())
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Ya hay una mision cooperativa en curso: %s de %s.", GetTitle(),
            PlayerName(ms_ownerId).c_str());
        CMissionHints::Help("Ya hay una mision cooperativa en curso: " + std::string(GetTitle()) + " de " + PlayerName(ms_ownerId) + ".");
        return false;
    }

    // starting a mission inside a cutscene
    if (CCutsceneMgr::ms_running)
        return false;

    return true;
}

CCoopMission::SChoice* CCoopMission::FindChoice(CRunningScript* pScript)
{
    for (auto& choice : ms_vChoices)
    {
        if (choice.pScript == pScript)
            return &choice;
    }
    return nullptr;
}

bool CCoopMission::ChooseSideMissionMode(CRunningScript* pScript, const char* szScriptName, const char* szTitle)
{
    if (pScript == nullptr)
        return true;

    // switch off: the script keeps playing on its own, exactly as before
    if (!CConfigLoader::ms_bCoopSideMissions)
        return true;

    // the races are not a synced coop mission: each player runs his own copy, the lobby only starts them together
    if (_strnicmp(szScriptName, "CPRACE", 6) == 0)
        return CVsLobby::Choose(pScript);

    SChoice* pChoice = FindChoice(pScript);
    if (pChoice == nullptr)
    {
        SChoice choice{};
        choice.pScript = pScript;
        CopyText(choice.szScriptName, sizeof(choice.szScriptName), szScriptName);
        CopyText(choice.szTitle, sizeof(choice.szTitle), szTitle);
        // individual-only missions and players that are alone (their choice or everybody else's) just play it
        const bool bAlone = IsIndividualOnlyScript(szScriptName) || CMissionMode::IsPlayingAlone();
        choice.eStage = (!bAlone && CanOfferCoop()) ? CHOICE_DECIDING : CHOICE_INDIVIDUAL;
        choice.nDeadline = GetTickCount() + CHOICE_TIMEOUT_MS;
        ms_vChoices.push_back(choice);
        pChoice = &ms_vChoices.back();
    }

    return pChoice->eStage == CHOICE_INDIVIDUAL || pChoice->eStage == CHOICE_COOP;
}

void CCoopMission::PruneChoices()
{
    for (auto it = ms_vChoices.begin(); it != ms_vChoices.end();)
    {
        if (it->pScript == nullptr || !IsScriptRunning(it->pScript))
            it = ms_vChoices.erase(it);
        else
            ++it;
    }
}

void CCoopMission::ResolveWaitingChoices(uint8_t eStage)
{
    for (auto it = ms_vChoices.begin(); it != ms_vChoices.end();)
    {
        SChoice& choice = *it;

        // F bug 1: the mission script may have died while the offer was pending (player death, cancelled side
        // mission). Writing its name or enabling its syncing then touches freed memory: the packets are processed
        // before PruneChoices runs, so drop the choice here exactly like PruneChoices does.
        if (choice.pScript == nullptr || !IsScriptRunning(choice.pScript))
        {
            it = ms_vChoices.erase(it);
            continue;
        }

        ++it;

        if (choice.eStage != CHOICE_WAITING)
            continue;

        choice.eStage = eStage;

        if (eStage != CHOICE_COOP)
            continue;

        // the server accepted the offer: the owner runs the coop mission, rename the running script and sync it
        strncpy_s(choice.pScript->m_szName, 8, choice.szScriptName, 7);
        EnableScriptSyncing(choice.pScript);
        ms_bOwnerMissionWasRunning = false;
    }
}

void CCoopMission::ProcessInvite(uint32_t now)
{
    if (!ms_bInvitePending)
        return;

    // cannot play (or ran out of time): answer "no" and let the server release the slot
    if ((int32_t)(now - ms_nInviteDeadline) >= 0 || COpCodeSync::IsLocalMissionRunning() || CCutsceneMgr::ms_running ||
        IsLocalPlayerDeadOrArrested())
    {
        ms_bInvitePending = false;
        ReplyToInvite(false);
        return;
    }

    if (IsMenuOrChatOpen())
        return;

    if (CPad::GetPad(0)->ConversationYesJustDown())
    {
        ms_bInvitePending = false;
        ReplyToInvite(true);
        return;
    }

    if (CPad::GetPad(0)->ConversationNoJustDown())
    {
        ms_bInvitePending = false;
        ReplyToInvite(false);
    }
}

void CCoopMission::ProcessChoices(uint32_t now)
{
    for (auto& choice : ms_vChoices)
    {
        if (choice.eStage == CHOICE_DECIDING)
        {
            if ((int32_t)(now - choice.nDeadline) >= 0)
            {
                choice.eStage = CHOICE_INDIVIDUAL;
                continue;
            }

            if (IsMenuOrChatOpen())
                continue;

            if (CPad::GetPad(0)->ConversationYesJustDown())
            {
                Packets::Scripts::CoopMissionOffer packet{};
                CopyText(packet.scriptName, sizeof(packet.scriptName), choice.szScriptName);
                CopyText(packet.title, sizeof(packet.title), choice.szTitle);
                GetPacketFactory().Send(packet);

                choice.eStage = CHOICE_WAITING;
                choice.nDeadline = now + OFFER_TIMEOUT_MS;
                choice.nSafetyDeadline = now + OFFER_SAFETY_TIMEOUT_MS;
            }
            else if (CPad::GetPad(0)->ConversationNoJustDown())
            {
                choice.eStage = CHOICE_INDIVIDUAL;
            }
        }
        else if (choice.eStage == CHOICE_WAITING)
        {
            // F bug 5: the mirrored server state decides, not the local clock. The owner's 12 s start before the offer
            // leaves, so the server's RUNNING arrives after this deadline; falling back to individual there would
            // leave the owner out of his own coop mission (the server would stay in PHASE_RUNNING until he leaves).
            const bool bOwnSlotTaken = IsActive() && ms_ownerId == CNetworkPlayerManager::m_localPlayerId;
            if (bOwnSlotTaken)
            {
                // OFFER: the invited players are still answering (OnState resolves it when RUNNING/FREE arrives);
                // the hard cap releases a mission the server never closes, e.g. one whose script is not syncing
                if ((int32_t)(now - choice.nSafetyDeadline) < 0)
                    continue;

                choice.eStage = CHOICE_INDIVIDUAL;
                SendEnd();  // let the server release the slot
                continue;
            }

            if ((int32_t)(now - choice.nDeadline) >= 0)
                choice.eStage = CHOICE_INDIVIDUAL;  // the server never answered: play it alone
        }
    }
}

void CCoopMission::ProcessAbandon(uint32_t now)
{
    if (!IsRunning() || !IsLocalParticipant())
    {
        ms_nAbandonHoldStart = 0;
        return;
    }

    // GetAsyncKeyState reads the whole system: with two games on one PC only the one with focus may see the key
    if (!CTestMode::IsGameWindowFocused() || IsMenuOrChatOpen() || (GetAsyncKeyState('2') & 0x8000) == 0)
    {
        ms_nAbandonHoldStart = 0;
        return;
    }

    if (ms_nAbandonHoldStart == 0)
    {
        ms_nAbandonHoldStart = now;
        return;
    }

    if (now - ms_nAbandonHoldStart < ABANDON_HOLD_MS)
        return;

    ms_nAbandonHoldStart = 0;

    if (IsOwner())
    {
        // the owner gives up: his mission fails and everyone is released through the normal end
        ms_bOwnerCancelled = true;
        RequestMissionFail();
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Cancelaste la mision: termina para todos los jugadores.");
        CMissionHints::Help("Cancelaste la mision: termina para todos los jugadores.");
        return;
    }

    // a participant just drops out, the mission goes on for the others
    LeaveMission();
}

void ClearNetworkMissionHud();  // PacketHandlers/scripts.cpp

void CCoopMission::LeaveMission(bool bFell)
{
    ms_bLocalLeft = true;
    Packets::Scripts::CoopMissionLeave packet{};
    packet.fell = bFell;
    GetPacketFactory().Send(packet);
    LocalCleanup();
    // the owner's timer and counters on the right of the screen are not his anymore
    ClearNetworkMissionHud();

    if (bFell)
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Caiste: la mision fallo para todos los jugadores.");
        CHud::SetHelpMessage("Caiste: la mision fallo para todos.", true, false, false);
    }
    else
    {
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Saliste de la mision. Los demas siguen jugandola.");
        CHud::SetHelpMessage("Saliste de la mision. Los demas siguen.", true, false, false);
    }
}

void CCoopMission::ProcessLocalFall()
{
    if (!IsRunning() || !IsLocalParticipant() || IsOwner())
        return;

    // a fallen guest leaves the mission and his own game carries on (hospital/police); the server tells the owner, who
    // fails the mission for everyone. ms_bLocalLeft keeps this from firing again while the server processes the leave.
    if (!IsLocalPlayerDeadOrArrested())
        return;

    LeaveMission(true);
}

void CCoopMission::ProcessEnd()
{
    if (!IsRunning() || !IsOwner())
    {
        if (!IsRunning())
            ms_bOwnerMissionWasRunning = false;
        return;
    }

    if (COpCodeSync::IsSyncedMissionRunning())
    {
        ms_bOwnerMissionWasRunning = true;
        ms_nOwnerRunningSince = GetTickCount();
        return;
    }

    if (!ms_bOwnerMissionWasRunning)
    {
        // the script never showed up (or ended before it was seen): don't leave the server with a mission that is gone
        const uint32_t now = GetTickCount();
        if (now - ms_nOwnerRunningSince > 8000)
        {
            ms_nOwnerRunningSince = now;
            SendEnd();
        }
        return;
    }

    // our mission ended: free the slot for everyone
    ms_bOwnerMissionWasRunning = false;
    SendEnd();
}

void CCoopMission::Process()
{
    // the players that connected during the mission take part in the next one
    if (!CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] ||
        !COpCodeSync::IsSyncedMissionRunning())
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
            pNetworkPlayer->m_bLateForMission = false;
    }

    if (!CConfigLoader::ms_bCoopSideMissions)
        return;

    if (!CNetwork::m_bAuthenticated)
        return;

    const uint32_t now = GetTickCount();

    PruneChoices();
    ProcessInvite(now);
    ProcessChoices(now);
    ProcessAbandon(now);
    ProcessLocalFall();
    ProcessEnd();
}

void CCoopMission::ReplyToInvite(bool bAccept)
{
    Packets::Scripts::CoopMissionReply packet{};
    packet.accept = bAccept;
    GetPacketFactory().Send(packet);
}

void CCoopMission::SendEnd()
{
    Packets::Scripts::CoopMissionEnd packet{};
    packet.cancelled = ms_bOwnerCancelled;
    ms_bOwnerCancelled = false;
    GetPacketFactory().Send(packet);
}

void CCoopMission::OnInvite(const Packets::Scripts::CoopMissionInvite& packet)
{
    if (!CConfigLoader::ms_bCoopSideMissions || !CNetwork::m_bAuthenticated)
        return;

    // already in a coop mission (or its owner): nothing to answer
    if (IsOwner() || IsLocalParticipant())
        return;

    // cannot join: answer "no" right away, exactly as the design asks
    if (CMissionMode::IsIndividual() || COpCodeSync::IsLocalMissionRunning() || CCutsceneMgr::ms_running ||
        IsLocalPlayerDeadOrArrested() || IsMenuOrChatOpen())
    {
        ReplyToInvite(false);
        return;
    }

    ms_bInvitePending = true;
    ms_inviteOwnerId = packet.owner;
    CopyText(ms_szInviteScriptName, sizeof(ms_szInviteScriptName), packet.scriptName);
    CopyText(ms_szInviteTitle, sizeof(ms_szInviteTitle), packet.title);

    uint32_t nTimeout = packet.timeoutMs;
    if (nTimeout < MIN_INVITE_MS)
        nTimeout = MIN_INVITE_MS;
    if (nTimeout > CCoopMission::OFFER_TIMEOUT_MS)
        nTimeout = CCoopMission::OFFER_TIMEOUT_MS;

    ms_nInviteDeadline = GetTickCount() + nTimeout;
}

void CCoopMission::OnState(const Packets::Scripts::CoopMissionState& packet)
{
    if (!CConfigLoader::ms_bCoopSideMissions)
        return;

    const bool bWasRunning = (ms_ePhase == PHASE_RUNNING);
    const bool bWasLocalParticipant = IsLocalParticipant();
    const bool bWasOwner = IsOwner();

    // a guest fell: the owner's mission is lost for all (its normal end releases everyone)
    if (packet.reason == Packets::Scripts::CoopMissionState::PARTICIPANT_FELL && bWasRunning && bWasOwner)
    {
        RequestMissionFail();
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Un companero cayo: la mision fallo para todos.");
        CMissionHints::Help("Un companero cayo: la mision fallo para todos.");
    }

    ms_ePhase = packet.phase;
    ms_ownerId = packet.owner;
    CopyText(ms_szScriptName, sizeof(ms_szScriptName), packet.scriptName);
    CopyText(ms_szTitle, sizeof(ms_szTitle), packet.title);

    ms_nParticipantCount = (packet.count <= MAX_PARTICIPANTS) ? packet.count : MAX_PARTICIPANTS;
    for (int i = 0; i < ms_nParticipantCount; i++)
    {
        ms_participants[i] = packet.participants[i];
    }

    if (packet.phase != Packets::Scripts::CoopMissionState::OFFER)
        ms_bInvitePending = false;

    if (packet.phase == Packets::Scripts::CoopMissionState::RUNNING)
    {
        // at least one guest accepted: the owner's side resolves to coop
        ResolveWaitingChoices(CHOICE_COOP);

        // only when the mission starts: a later state (a guest left or fell) can arrive in the very frame the owner's script
        // ends, and clearing it then would make ProcessEnd never tell the server the mission is over
        if (!bWasRunning)
        {
            ms_bOwnerMissionWasRunning = false;
            ms_nOwnerRunningSince = GetTickCount();
        }

        // N_busted: the authority reported our fall and the server dropped us from the mission: drop the mission
        // state it left on us (OnAMissionFlag, checkpoints, blips, mission HUD) instead of staying stuck in it
        if (bWasRunning && bWasLocalParticipant && !IsOwner() && !IsLocalParticipant())
            LocalCleanup();

        return;
    }

    if (packet.phase == Packets::Scripts::CoopMissionState::FREE)
    {
        // the slot was released (nobody accepted, it was busy, the owner left, it ended...)
        ResolveWaitingChoices(CHOICE_INDIVIDUAL);

        // a mission we were playing ended or was aborted: drop the state it left on this client
        if (bWasRunning && bWasLocalParticipant && !bWasOwner)
            LocalCleanup();

        // the owner's last "no clock" packet can arrive after the server freed the mission and be dropped there
        if (bWasRunning && !bWasOwner)
            ClearNetworkMissionHud();

        // the owner gave it up or left: tell the ones still in it (the one that dropped out already knows)
        if (bWasRunning && bWasLocalParticipant && !bWasOwner && !ms_bLocalLeft)
        {
            if (packet.reason == Packets::Scripts::CoopMissionState::OWNER_LEFT)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + PlayerName(packet.owner) + " cancelo la mision: termina para todos.");
                CMissionHints::Help(PlayerName(packet.owner) + " cancelo la mision: termina para todos.");
            }
            else if (packet.reason == Packets::Scripts::CoopMissionState::DISCONNECT)
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + PlayerName(packet.owner) + " se desconecto: la mision termino.");
                CMissionHints::Help(PlayerName(packet.owner) + " se desconecto: la mision termino.");
            }
        }

        ms_bLocalLeft = false;
        ms_bOwnerMissionWasRunning = false;
        ms_bForceFail = false;
    }
}

bool CCoopMission::ShouldDraw()
{
    if (!CConfigLoader::ms_bCoopSideMissions)
        return false;

    if (ms_bInvitePending)
        return true;

    for (const auto& choice : ms_vChoices)
    {
        if (choice.eStage == CHOICE_DECIDING || choice.eStage == CHOICE_WAITING)
            return true;
    }

    return IsRunning() && (IsLocalParticipant() || IsOwner());
}

void CCoopMission::Draw()
{
    // readable at 1080p and above without covering the screen (1.0 at 720p, ~1.65 at 1080p)
    const float scale = (std::max)(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f) * 1.1f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs;
    const ImVec2 pos(ImGui::GetIO().DisplaySize.x * 0.5f, 80.0f * scale);

    if (ms_bInvitePending)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::Begin("Mision cooperativa", nullptr, flags);
        ImGui::SetWindowFontScale(scale);

        ImGui::TextColored(ImVec4(0.79f, 0.65f, 1.0f, 1.0f),
            "Queres unirte a la mision cooperativa %s de %s?", ms_szInviteTitle,
            PlayerName(ms_inviteOwnerId).c_str());
        ImGui::TextUnformatted("Y (Si) / N (No)");

        ImGui::End();
        return;
    }

    for (const auto& choice : ms_vChoices)
    {
        if (choice.eStage != CHOICE_DECIDING && choice.eStage != CHOICE_WAITING)
            continue;

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::Begin("Mision cooperativa", nullptr, flags);
        ImGui::SetWindowFontScale(scale);

        if (choice.eStage == CHOICE_DECIDING)
        {
            ImGui::TextUnformatted("Y = cooperativa / N = individual");
        }
        else
        {
            ImGui::Text("Esperando respuestas (%d/%d)", ms_nParticipantCount,
                (int)CNetworkPlayerManager::m_pPlayers.size() + 1);
        }

        ImGui::End();
        return;
    }

    if (!IsRunning() || (!IsLocalParticipant() && !IsOwner()))
        return;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::Begin("Mision cooperativa", nullptr, flags);
    ImGui::SetWindowFontScale(scale);

    ImGui::TextUnformatted("Mantene 2 para abandonar la mision");

    if (ms_nAbandonHoldStart != 0)
    {
        const float progress = (std::min)(1.0f, (float)(GetTickCount() - ms_nAbandonHoldStart) / (float)ABANDON_HOLD_MS);
        ImGui::ProgressBar(progress, ImVec2(200.0f * scale, 0.0f));
    }

    ImGui::End();
}

void CCoopMission::LocalCleanup()
{
    if (CTheScripts::OnAMissionFlag)
        CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] = 0;

    CNetworkCheckpoint::Remove();
    CMissionAreaMarkers::Clear();
    CMirrorRaceCheckpoints::Clear();
    CNetworkEntityBlip::ClearEntityBlips();
    TheCamera.SetWideScreenOff();
    CPad::GetPad(0)->SetDrunkInputDelay(0);
    CPad::GetPad(0)->DisablePlayerControls = 0;
    CPad::GetPad(0)->bApplyBrakes = 0;
    CPad::GetPad(0)->bDisablePlayerEnterCar = 0;
    CPad::GetPad(0)->bDisablePlayerDuck = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerFireWeaponWithL1 = 0;
    CPad::GetPad(0)->bDisablePlayerCycleWeapon = 0;
    CPad::GetPad(0)->bDisablePlayerJump = 0;
    CPad::GetPad(0)->bDisablePlayerDisplayVitalStats = 0;
    CDraw::FadeValue = 0;
}
