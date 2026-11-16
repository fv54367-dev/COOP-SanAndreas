#include "stdafx.h"

std::vector<CNetworkPlayer*> CNetworkPlayerManager::m_pPlayers;
CPad CNetworkPlayerManager::m_pPads[Config::MAX_SERVER_PLAYERS + 2];
playerid_t CNetworkPlayerManager::m_localPlayerId;

void CNetworkPlayerManager::Add(CNetworkPlayer* player)
{
    m_pPlayers.push_back(player);
}

void CNetworkPlayerManager::Remove(CNetworkPlayer* player)
{
    auto it = std::find(m_pPlayers.begin(), m_pPlayers.end(), player);
    if (it != m_pPlayers.end())
    {
        m_pPlayers.erase(it);
    }
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(SenderPlayerId senderPlayerid)
{
    return GetPlayer(senderPlayerid.value);
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(playerid_t playerid)
{
    if (!playerid.valid())
        return nullptr;

    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_playerId == playerid)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(CEntity* entity)
{
    if (entity == nullptr)
        return nullptr;

    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_pPed == entity)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

int CNetworkPlayerManager::GetFreeSlot()
{
    for (int slot = 0; slot < Config::MAX_SERVER_PLAYERS; slot++)
    {
        bool bUsed = false;
        for (CNetworkPlayer* pNetworkPlayer : m_pPlayers)
        {
            if (pNetworkPlayer->m_nSlot == slot)
            {
                bUsed = true;
                break;
            }
        }

        if (!bUsed)
            return slot;
    }

    return -1;
}

static constexpr uint32_t AUTO_REFRESH_AFTER_MS = 4000;

int CNetworkPlayerManager::RefreshLookingDead(std::vector<std::string>* names)
{
    int count = 0;
    // Refresh can rebuild a ped but never adds/removes players, still iterate a copy
    std::vector<CNetworkPlayer*> players = m_pPlayers;
    for (CNetworkPlayer* player : players)
    {
        if (player == nullptr || player->m_playerId == m_localPlayerId)
            continue;
        if (player->Refresh(true))
        {
            count++;
            if (names)
                names->push_back(player->GetName());
        }
    }
    return count;
}

void CNetworkPlayerManager::ProcessAutoRefresh()
{
    if (!CConfigLoader::ms_bRemoteReviveAuto)
        return;

    uint32_t now = GetTickCount();
    std::vector<CNetworkPlayer*> players = m_pPlayers;
    for (CNetworkPlayer* player : players)
    {
        if (player == nullptr || player->m_playerId == m_localPlayerId)
            continue;

        if (!player->LooksDeadButAlive())
        {
            player->m_looksDeadSince = 0;
            continue;
        }

        if (player->m_looksDeadSince == 0)
        {
            player->m_looksDeadSince = now;
        }
        else if (now - player->m_looksDeadSince >= AUTO_REFRESH_AFTER_MS)
        {
            player->Refresh(false);
        }
    }
}
