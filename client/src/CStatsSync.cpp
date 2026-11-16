#include "stdafx.h"

int m_anStoredIntStats[224];
float m_afStoredFloatStats[83];

constexpr int MAX_INT_STATS = sizeof(m_anStoredIntStats) / sizeof(int);
constexpr int MAX_FLOAT_STATS = sizeof(m_afStoredFloatStats) / sizeof(float);

std::array<eStats, CStatsSync::SYNCED_STATS_COUNT> CStatsSync::m_aeSyncedStats =
{
    STAT_PISTOL_SKILL,
    STAT_SILENCED_PISTOL_SKILL,
    STAT_DESERT_EAGLE_SKILL,
    STAT_SHOTGUN_SKILL,
    STAT_SAWN_OFF_SHOTGUN_SKILL,
    STAT_COMBAT_SHOTGUN_SKILL,
    STAT_MACHINE_PISTOL_SKILL,
    STAT_SMG_SKILL,
    STAT_AK_47_SKILL,
    STAT_M4_SKILL,
    STAT_RIFLE_SKILL,
    STAT_MAX_HEALTH
};

void CStatsSync::ApplyNetworkPlayerContext(CNetworkPlayer* player)
{
    for (int i = 0; i < MAX_INT_STATS; ++i)
        m_anStoredIntStats[i] = CStats::StatTypesInt[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        m_afStoredFloatStats[i] = CStats::StatTypesFloat[i];

    for (int i = 0; i < MAX_INT_STATS; ++i)
        CStats::StatTypesInt[i] = player->m_stats.m_aStatsInt[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        CStats::StatTypesFloat[i] = player->m_stats.m_aStatsFloat[i];
}

void CStatsSync::ApplyLocalContext()
{
    for (int i = 0; i < MAX_INT_STATS; ++i)
        CStats::StatTypesInt[i] = m_anStoredIntStats[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        CStats::StatTypesFloat[i] = m_afStoredFloatStats[i];
}


int m_anSavedLocalIntStats[MAX_INT_STATS];
float m_afSavedLocalFloatStats[MAX_FLOAT_STATS];

void CStatsSync::SaveLocalStats()
{
    if (ms_nRemoteProcessingDepth++ > 0)
        return;

    for (int i = 0; i < MAX_INT_STATS; ++i)
        m_anSavedLocalIntStats[i] = CStats::StatTypesInt[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        m_afSavedLocalFloatStats[i] = CStats::StatTypesFloat[i];
}

void CStatsSync::RestoreLocalStats()
{
    if (ms_nRemoteProcessingDepth == 0 || --ms_nRemoteProcessingDepth > 0)
        return;

    for (int i = 0; i < MAX_INT_STATS; ++i)
        CStats::StatTypesInt[i] = m_anSavedLocalIntStats[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        CStats::StatTypesFloat[i] = m_afSavedLocalFloatStats[i];
}

CPedClothesDesc m_lastSentClothes{};
bool m_bClothesSent = false;

void CStatsSync::SendClothes()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer || !pPlayer->m_pPlayerData || !pPlayer->m_pPlayerData->m_pPedClothesDesc)
        return;

    Packets::Players::RebuildPlayer packet{};
    packet.clothesDesc = *pPlayer->m_pPlayerData->m_pPedClothesDesc;
    GetPacketFactory().Send(packet);

    m_lastSentClothes = packet.clothesDesc;
    m_bClothesSent = true;
}

void CStatsSync::ProcessClothes()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer || !pPlayer->m_pPlayerData || !pPlayer->m_pPlayerData->m_pPedClothesDesc)
        return;

    if (m_bClothesSent &&
        memcmp(pPlayer->m_pPlayerData->m_pPedClothesDesc, &m_lastSentClothes, sizeof(CPedClothesDesc)) == 0)
        return;

    SendClothes();
}

void CStatsSync::ResetClothes()
{
    m_bClothesSent = false;
}

void CStatsSync::NotifyChanged()
{
    // stat changes made while processing a network player are discarded, don't send them
    if (ms_nRemoteProcessingDepth > 0)
        return;

    Packets::Players::PlayerStats packet{};
    
    for (uint8_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
    {
        packet.stats[i] = CStats::GetStatValue(m_aeSyncedStats[i]);
    }

    GetPacketFactory().Send(packet);
}

int CStatsSync::GetSyncIdByInternal(eStats stat)
{
    for (int i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
    {
        if (m_aeSyncedStats[i] == stat)
            return i;
    }

    return -1;
}