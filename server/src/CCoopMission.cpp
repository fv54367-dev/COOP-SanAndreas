#include "stdafx.h"

#include "CCoopMission.h"

#include "network/packets/scripts.h"
#include <cstring>

namespace
{
void CopyText(char* szDst, size_t dstSize, const char* szSrc)
{
    if (dstSize == 0)
        return;

    if (szSrc == nullptr)
    {
        szDst[0] = '\0';
        return;
    }

    size_t len = strnlen(szSrc, dstSize - 1);
    memcpy(szDst, szSrc, len);
    szDst[len] = '\0';
}
}  // namespace

void CCoopMission::Process()
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    if (ms_ePhase == PHASE_OFFER && (int32_t)(g_serverTime - ms_nOfferDeadline) >= 0)
    {
        // the invited players ran out of time: the ones that accepted stay, the rest are out
        FinishOffer();
    }
}

bool CCoopMission::IsActive()
{
    return CConfigManager::GetConfigCoopSideMissions() && ms_ePhase != PHASE_FREE;
}

bool CCoopMission::IsRunning()
{
    return CConfigManager::GetConfigCoopSideMissions() && ms_ePhase == PHASE_RUNNING;
}

uint8_t CCoopMission::GetPhase()
{
    return ms_ePhase;
}

playerid_t CCoopMission::GetOwnerId()
{
    return ms_ownerId;
}

const char* CCoopMission::GetTitle()
{
    return ms_szTitle;
}

bool CCoopMission::IsScriptAuthority(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
        return false;

    // while the coop mission runs its owner runs the script; during the 12 s offer (nothing is synced yet) and with
    // no mission at all the host keeps it, so a story mission in flight is never interrupted
    if (IsRunning())
        return pNetworkPlayer->m_playerId == ms_ownerId;

    return pNetworkPlayer->m_bIsHost;
}

bool CCoopMission::IsOwner(CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && IsActive() && pNetworkPlayer->m_playerId == ms_ownerId;
}

bool CCoopMission::IsParticipant(CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && IsParticipant(pNetworkPlayer->m_playerId);
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

bool CCoopMission::IsOutOfHostMission(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIndividualMode)
        return true;
    // the original scripts and ours run other missions
    CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost();
    return pHost != nullptr && pHost != pNetworkPlayer && pHost->m_bVanillaScripts != pNetworkPlayer->m_bVanillaScripts;
}

bool CCoopMission::CanReceiveMissionPacket(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
        return false;

    // outside a running coop mission the mission packets go to everyone but the players that connected in the middle of
    // it and the ones that play alone (F1 individual: none of the authority's mission reaches them)
    if (!IsRunning())
        return !pNetworkPlayer->m_bLateForMission && !IsOutOfHostMission(pNetworkPlayer);

    return IsParticipant(pNetworkPlayer);
}

void CCoopMission::Relay(Packet& packet, CNetworkPlayer* pSender, CNetworkPlayer* pNetworkPlayerToIgnore, bool bWorldState)
{
    // only the owner of a running coop mission is filtered; anything else (the host's story missions, world state)
    // goes to everyone, as today
    if (IsRunning() && IsOwner(pSender))
    {
        SendToParticipants(packet, pNetworkPlayerToIgnore);
        return;
    }

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer == pNetworkPlayerToIgnore || pNetworkPlayer->m_bLateForMission)
            continue;
        // the host's mission (texts, cutscenes, camera, blips, opcodes) doesn't reach a player in individual mode;
        // the world state of its objects and vehicles does
        if (!bWorldState && IsOutOfHostMission(pNetworkPlayer))
            continue;
        GetPacketFactory().Send(packet, pNetworkPlayer);
    }
}

bool CCoopMission::IsAuthorityOnMission()
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_bOnMission && IsScriptAuthority(pNetworkPlayer))
            return true;
    }
    return false;
}

void CCoopMission::ClearLatePlayers()
{
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        pNetworkPlayer->m_bLateForMission = false;
}

void CCoopMission::SendToParticipants(Packet& packet, CNetworkPlayer* pNetworkPlayerToIgnore)
{
    for (int i = 0; i < ms_nParticipantCount; i++)
    {
        if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(ms_participants[i]))
        {
            if (pNetworkPlayer != pNetworkPlayerToIgnore)
                GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }
}

void CCoopMission::OnOffer(CNetworkPlayer* pNetworkPlayer, const char* szScriptName, const char* szTitle)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    if (pNetworkPlayer == nullptr)
        return;

    // only one coop mission at a time: the offering player gets the active mission back with reason "busy" (everyone
    // already knows that state, it is broadcast while it is active)
    if (ms_ePhase != PHASE_FREE)
    {
        SendStateTo(pNetworkPlayer, REASON_BUSY);
        return;
    }

    ms_ePhase = PHASE_OFFER;
    ms_ownerId = pNetworkPlayer->m_playerId;
    CopyText(ms_szScriptName, ARRAY_SIZE(ms_szScriptName), szScriptName);
    CopyText(ms_szTitle, ARRAY_SIZE(ms_szTitle), szTitle);

    ms_nParticipantCount = 0;
    AddParticipant(ms_ownerId);

    ms_vPendingReplies.clear();
    for (auto* pOtherPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pOtherPlayer == pNetworkPlayer)
            continue;

        ms_vPendingReplies.push_back(pOtherPlayer->m_playerId);

        Packets::Scripts::CoopMissionInvite invite{};
        invite.owner = ms_ownerId;
        CopyText(invite.scriptName, ARRAY_SIZE(invite.scriptName), ms_szScriptName);
        CopyText(invite.title, ARRAY_SIZE(invite.title), ms_szTitle);
        invite.timeoutMs = (uint16_t)OFFER_TIMEOUT_MS;
        GetPacketFactory().Send(invite, pOtherPlayer);
    }

    logger::info("%s offered the coop mission %s (%s)", pNetworkPlayer->GetName().c_str(), ms_szScriptName,
        ms_szTitle);

    // nobody to invite: the mission stays individual
    if (ms_vPendingReplies.empty())
    {
        FinishOffer();
        return;
    }

    ms_nOfferDeadline = g_serverTime + OFFER_TIMEOUT_MS;
    SendState(REASON_OK);
}

void CCoopMission::OnReply(CNetworkPlayer* pNetworkPlayer, bool bAccept)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    if (pNetworkPlayer == nullptr || ms_ePhase != PHASE_OFFER)
        return;

    auto it = std::find(ms_vPendingReplies.begin(), ms_vPendingReplies.end(), pNetworkPlayer->m_playerId);
    if (it == ms_vPendingReplies.end())
        return;  // not invited (or answered twice): ignore

    ms_vPendingReplies.erase(it);

    if (bAccept)
    {
        AddParticipant(pNetworkPlayer->m_playerId);
        SendState(REASON_OK);
    }

    if (ms_vPendingReplies.empty())
    {
        FinishOffer();
    }
}

void CCoopMission::OnLeave(CNetworkPlayer* pNetworkPlayer, bool bFell)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    if (pNetworkPlayer == nullptr || !IsRunning() || !IsParticipant(pNetworkPlayer))
        return;

    if (pNetworkPlayer->m_playerId == ms_ownerId)
    {
        // the owner giving up ends the mission for everyone (his game fails it)
        SendState(REASON_OWNER_LEFT);
        Reset();
        return;
    }

    // a participant leaving on his own drops out and the mission goes on for the others; one that fell (dead or arrested)
    // makes the owner fail it for everyone (docs/REQUISITOS.md 1)
    RemoveParticipant(pNetworkPlayer->m_playerId);

    logger::info("%s %s the coop mission %s", pNetworkPlayer->GetName().c_str(), bFell ? "fell in" : "left", ms_szScriptName);
    SendState(bFell ? REASON_PARTICIPANT_FELL : REASON_OK);
}

void CCoopMission::OnEnd(CNetworkPlayer* pNetworkPlayer, bool bPassed, bool bCancelled)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    // only the owner decides that his mission ended
    if (pNetworkPlayer == nullptr || !IsRunning() || pNetworkPlayer->m_playerId != ms_ownerId)
        return;

    logger::info("the coop mission %s ended (%s)", ms_szScriptName, bCancelled ? "cancelled by the owner" : bPassed ? "passed" : "failed");
    SendState(bCancelled ? REASON_OWNER_LEFT : REASON_FINISHED);
    Reset();
}

void CCoopMission::SendCurrentStateTo(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr || !IsActive())
        return;

    // the mission was already announced to everyone; a late joiner just learns it (he is not a participant)
    SendStateTo(pNetworkPlayer, REASON_OK);
}

void CCoopMission::OnPlayerLeft(CNetworkPlayer* pNetworkPlayer)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    if (pNetworkPlayer == nullptr)
        return;

    if (ms_ePhase == PHASE_OFFER)
    {
        // the owner leaving during the offer releases the slot; an invitee leaving is one answer less to wait for
        if (pNetworkPlayer->m_playerId == ms_ownerId)
        {
            SendState(REASON_DISCONNECT);
            Reset();
            return;
        }

        auto it = std::find(ms_vPendingReplies.begin(), ms_vPendingReplies.end(), pNetworkPlayer->m_playerId);
        if (it != ms_vPendingReplies.end())
        {
            ms_vPendingReplies.erase(it);
            if (ms_vPendingReplies.empty())
            {
                FinishOffer();
            }
        }
        return;
    }

    if (ms_ePhase != PHASE_RUNNING || !IsParticipant(pNetworkPlayer))
        return;

    if (pNetworkPlayer->m_playerId == ms_ownerId)
    {
        // the owner is gone: the mission closes for everyone (his peds and vehicles are removed on disconnect)
        SendState(REASON_DISCONNECT);
        Reset();
        return;
    }

    RemoveParticipant(pNetworkPlayer->m_playerId);
    SendState(REASON_OK);
}

void CCoopMission::Reset()
{
    ms_ePhase = PHASE_FREE;
    ms_ownerId = playerid_t();
    ms_szScriptName[0] = '\0';
    ms_szTitle[0] = '\0';
    ms_nParticipantCount = 0;
    for (auto& participant : ms_participants)
    {
        participant = playerid_t();
    }
    ms_vPendingReplies.clear();
    ms_nOfferDeadline = 0;
}

void CCoopMission::FinishOffer()
{
    // at least one guest accepted: the mission starts for real (the owner's client renames and syncs its script)
    if (ms_nParticipantCount >= 2)
    {
        ms_ePhase = PHASE_RUNNING;
        ms_vPendingReplies.clear();
        ms_nOfferDeadline = 0;

        logger::info("the coop mission %s started with %d participants", ms_szScriptName, ms_nParticipantCount);
        SendState(REASON_OK);
        return;
    }

    // nobody accepted: the slot is released and the owner plays it on his own
    SendState(REASON_NOBODY_ACCEPTED);
    Reset();
}

void CCoopMission::AddParticipant(playerid_t playerid)
{
    if (ms_nParticipantCount >= MAX_PARTICIPANTS || IsParticipant(playerid))
        return;

    ms_participants[ms_nParticipantCount++] = playerid;
}

void CCoopMission::RemoveParticipant(playerid_t playerid)
{
    for (int i = 0; i < ms_nParticipantCount; i++)
    {
        if (ms_participants[i] != playerid)
            continue;

        for (int j = i; j + 1 < ms_nParticipantCount; j++)
        {
            ms_participants[j] = ms_participants[j + 1];
        }
        ms_participants[--ms_nParticipantCount] = playerid_t();
        return;
    }
}

void CCoopMission::SendState(eReason reason)
{
    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        SendStateTo(pNetworkPlayer, reason);
    }
}

void CCoopMission::SendStateTo(CNetworkPlayer* pNetworkPlayer, eReason reason)
{
    if (pNetworkPlayer == nullptr)
        return;

    Packets::Scripts::CoopMissionState state{};
    FillState(state, reason);
    GetPacketFactory().Send(state, pNetworkPlayer);
}

void CCoopMission::FillState(Packets::Scripts::CoopMissionState& state, eReason reason)
{
    // a terminal answer (nobody accepted, the owner left, a disconnect, the mission ended) means the slot is free:
    // the owner/participants still travel so the clients can tell which mission it was and clean up after it
    const bool bTerminal = reason == REASON_NOBODY_ACCEPTED || reason == REASON_OWNER_LEFT ||
                           reason == REASON_DISCONNECT || reason == REASON_FINISHED;

    state.phase = bTerminal ? Packets::Scripts::CoopMissionState::FREE : ms_ePhase;
    state.owner = ms_ownerId;
    CopyText(state.scriptName, ARRAY_SIZE(state.scriptName), ms_szScriptName);
    CopyText(state.title, ARRAY_SIZE(state.title), ms_szTitle);
    state.count = (uint8_t)ms_nParticipantCount;
    for (int i = 0; i < ms_nParticipantCount; i++)
    {
        state.participants[i] = ms_participants[i];
    }
    state.reason = reason;
}
