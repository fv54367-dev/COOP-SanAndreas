#pragma once
class CStatsSync
{
public:
	static constexpr inline int SYNCED_STATS_COUNT = 14;

	static std::array<eStats, SYNCED_STATS_COUNT> m_aeSyncedStats;
	static void ApplyNetworkPlayerContext(CNetworkPlayer* player);
	static void ApplyLocalContext();
	static void NotifyChanged();
	static int GetSyncIdByInternal(eStats stat);

	// while a network player's ped/vehicle is processed the game writes into the global CStats;
	// these keep the local stats untouched (skills and stats are per player)
	static void SaveLocalStats();
	static void RestoreLocalStats();
	static inline int ms_nRemoteProcessingDepth = 0;

	// clothes/body of the local player; resent whenever they differ from the last sent ones
	// (loading a savegame rebuilds the player without the Dress hook)
	static void SendClothes();
	static void ProcessClothes();
	static void ResetClothes();
};

