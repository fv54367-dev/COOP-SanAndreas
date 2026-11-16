#include "stdafx.h"
#include "CMissionLock.h"
#include "network/packets/scripts.h"
#include "mission_ids.h"

void CMissionLock::Broadcast(int missionId, bool locked, playerid_t ownerId)
{
    Packets::Scripts::MissionLockState packet{};
    packet.missionId = missionId;
    packet.locked = locked;
    packet.ownerId = ownerId;
    GetPacketFactory().SendToAll(packet);
}

void CMissionLock::OnRequest(CNetworkPlayer* pNetworkPlayer, int missionId)
{
    if (pNetworkPlayer == nullptr || missionId < 0 || missionId >= MAX_MISSIONS)
        return;

    if (!CConfigManager::GetConfigMissionLock() || IsSideMissionId(missionId))
        return;

    const playerid_t owner = ms_owner[missionId];

    // free, or already his (a restart of the same mission)
    if (!owner.valid() || owner == pNetworkPlayer->m_playerId)
    {
        ms_owner[missionId] = pNetworkPlayer->m_playerId;
        Broadcast(missionId, true, pNetworkPlayer->m_playerId);
        return;
    }

    // taken by someone else: only the requester needs to know who has it
    Packets::Scripts::MissionLockState packet{};
    packet.missionId = missionId;
    packet.locked = true;
    packet.ownerId = owner;
    GetPacketFactory().Send(packet, pNetworkPlayer);
}

void CMissionLock::OnRelease(CNetworkPlayer* pNetworkPlayer, int missionId)
{
    if (pNetworkPlayer == nullptr || missionId < 0 || missionId >= MAX_MISSIONS)
        return;

    if (ms_owner[missionId] != pNetworkPlayer->m_playerId)
        return;

    ms_owner[missionId] = playerid_t{};
    Broadcast(missionId, false, playerid_t{});
}

void CMissionLock::OnPlayerLeft(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
        return;

    for (int i = 0; i < MAX_MISSIONS; i++)
    {
        if (ms_owner[i] == pNetworkPlayer->m_playerId)
        {
            ms_owner[i] = playerid_t{};
            Broadcast(i, false, playerid_t{});
        }
    }
}

void CMissionLock::SendSnapshot(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
        return;

    for (int i = 0; i < MAX_MISSIONS; i++)
    {
        if (!ms_owner[i].valid())
            continue;

        Packets::Scripts::MissionLockState packet{};
        packet.missionId = i;
        packet.locked = true;
        packet.ownerId = ms_owner[i];
        GetPacketFactory().Send(packet, pNetworkPlayer);
    }
}
