#pragma once

class CNetworkStaticBlip
{
public:
	//static inline std::vector<SMissionMarker> ms_vMissionMarkers;
	static inline bool ms_bNeedToSendAfterThisFrame = true;

	// blips created by a mission script (`CRadar::ms_RadarTrace` index): they belong to one player's mission, so the
	// host does not send them in the world snapshot and a received snapshot does not clear them (see RadarHooks).
	// Holds the blip counter of the slot when it was marked (0 = not a mission blip): every new blip in a slot gets a new
	// counter (CRadar::GetNewUniqueBlipIndex 0x582820), so a blip created or cleared without our hooks drops the mark
	static inline uint16_t ms_anMissionBlipCounter[MAX_RADAR_TRACES] = {};

	// blips created by the host's synced mission script: they travel in the snapshot (the players of the mission see them)
	// but the server leaves them out for the players outside the mission (connected in the middle of it, individual)
	static inline uint16_t ms_anSyncedMissionBlipCounter[MAX_RADAR_TRACES] = {};

	// own-static-blips: the handles this game made from the host's snapshot (only those are replaced by the next one)
	static inline std::vector<int> ms_aSnapshotHandles;

	// own-static-blips: the host's last snapshot, kept to make its blips again after a save and for contacts-follow-host
	static inline std::vector<Packets::Blips::_StaticBlipPayload> ms_aHostBlips;
	static inline bool ms_bHaveHostBlips = false;

	// intro-follow-host: the intro counter raised to the host's (not saved: GenericSave_OwnProgress puts the own one back)
	static inline int ms_nIntroOwnCounter = -1;
	static inline int ms_nIntroCopiedCounter = -1;
	// $flag_intro_mission_counter to `value` with the contact blip INT/INTROST leave for it (none at 2)
	static void SetIntroCounter(int value);

	static void Create(const Packets::Blips::StaticBlipsSnapshot& packet);
	static void Send();

	// every frame (contacts-follow-host): a guest playing coop sees the host's contact points instead of his own
	static void Process();
	// around GenericSave: the save holds this game's own icons only (nothing made from the host's snapshot)
	static void BeforeSave();
	static void AfterSave();

	static inline void MarkMissionBlip(int index, bool mission)
	{
		if (index >= 0 && index < MAX_RADAR_TRACES)
			ms_anMissionBlipCounter[index] = mission ? CRadar::ms_RadarTrace[index].m_nCounter : 0;
	}

	static inline bool IsMissionBlip(int index)
	{
		return index >= 0 && index < MAX_RADAR_TRACES && ms_anMissionBlipCounter[index] != 0 &&
			ms_anMissionBlipCounter[index] == CRadar::ms_RadarTrace[index].m_nCounter;
	}

	static inline void MarkSyncedMissionBlip(int index, bool mission)
	{
		if (index >= 0 && index < MAX_RADAR_TRACES)
			ms_anSyncedMissionBlipCounter[index] = mission ? CRadar::ms_RadarTrace[index].m_nCounter : 0;
	}

	static inline bool IsSyncedMissionBlip(int index)
	{
		return index >= 0 && index < MAX_RADAR_TRACES && ms_anSyncedMissionBlipCounter[index] != 0 &&
			ms_anSyncedMissionBlipCounter[index] == CRadar::ms_RadarTrace[index].m_nCounter;
	}

	static inline bool IsAllowedSyncingRadarSprite(eRadarSprite sprite)
	{
		return sprite == 0 || sprite == 1 || (sprite >= 5 && sprite <= 40) || (sprite >= 42 && sprite <= 55) || (sprite >= 58 && sprite <= 63);
	}
};

