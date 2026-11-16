#include "stdafx.h"
#include "CSaveManager.h"
#include "CPersistence.h"
#include "CStringUtil.h"
#include "network/packets/players.h"
#include "network/packets/system.h"

static constexpr uint32_t SAVE_RESULT_TIMEOUT_MS = 5000;

void CSaveManager::Process()
{
    if (ms_bSaving && (int32_t)(g_serverTime - ms_nDeadline) >= 0)
    {
        Finish(true);
    }

    uint32_t minutes = CConfigManager::GetConfigAutosaveMinutes();
    if (minutes == 0 || ms_bSaving)
        return;

    if (ms_nLastAutoSave == 0)
    {
        ms_nLastAutoSave = g_serverTime;
        return;
    }

    if (g_serverTime - ms_nLastAutoSave < minutes * 60000u)
        return;

    ms_nLastAutoSave = g_serverTime;
    RequestSaveAll(nullptr);
}

void CSaveManager::RequestSaveAll(CNetworkPlayer* pRequester)
{
    if (!CPersistence::IsEnabled())
    {
        // only the player that asked for it cares (the autosave just stays quiet)
        if (pRequester != nullptr)
        {
            ms_requesterId = pRequester->m_playerId;
            SendChatResult("La persistencia esta apagada en el servidor.");
            ms_requesterId = playerid_t();
        }
        return;
    }

    ms_pendingPlayers.clear();
    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        ms_pendingPlayers.push_back(pNetworkPlayer->m_playerId);
    }

    ms_requesterId = pRequester != nullptr ? pRequester->m_playerId : playerid_t();
    ms_nDeadline = g_serverTime + SAVE_RESULT_TIMEOUT_MS;
    ms_bSaving = true;

    logger::info("save all: requested the state of %u players", (unsigned)ms_pendingPlayers.size());

    Packets::Players::PersistSaveRequest request{};
    GetPacketFactory().SendToAll(request);

    // nobody connected: nothing to wait for
    if (ms_pendingPlayers.empty())
        Finish(false);
}

void CSaveManager::NotifySaved(CNetworkPlayer* pNetworkPlayer)
{
    if (!ms_bSaving)
        return;

    auto it = std::find(ms_pendingPlayers.begin(), ms_pendingPlayers.end(), pNetworkPlayer->m_playerId);
    if (it == ms_pendingPlayers.end())
        return;  // a regular periodic update, not part of this save

    ms_pendingPlayers.erase(it);

    if (ms_pendingPlayers.empty())
        Finish(false);
}

// E bug 6: a player that disconnects in the middle of a save-all is not answering anymore; without this his id would
// stay pending until the deadline and the requester would be told the save failed
void CSaveManager::OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer)
{
    if (!ms_bSaving || pNetworkPlayer == nullptr)
        return;

    auto it = std::find(ms_pendingPlayers.begin(), ms_pendingPlayers.end(), pNetworkPlayer->m_playerId);
    if (it == ms_pendingPlayers.end())
        return;

    ms_pendingPlayers.erase(it);

    if (ms_pendingPlayers.empty())
        Finish(false);
}

void CSaveManager::Finish(bool bTimedOut)
{
    ms_bSaving = false;
    ms_pendingPlayers.clear();

    if (bTimedOut)
        SendChatResult("No se pudo guardar: algunos jugadores no respondieron.");
    else
        SendChatResult("Partida guardada en el server.");

    if (bTimedOut)
        logger::warn("save all: timed out before every player answered");
    else
        logger::info("save all: every player was saved");

    ms_requesterId = playerid_t();
}

void CSaveManager::SendChatResult(const char* text)
{
    if (!ms_requesterId.valid())
        return;

    CNetworkPlayer* pRequester = CNetworkPlayerManager::GetPlayer(ms_requesterId);
    if (pRequester == nullptr)
        return;

    Packets::System::ServerChat chat{};
    ServerTextToWide(text, chat.message, ARRAY_SIZE(chat.message));
    GetPacketFactory().Send(chat, pRequester);
}
