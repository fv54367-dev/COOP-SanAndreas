#pragma once

class CNetworkPlayer
{
public:
	CPlayerPed* m_pPed = nullptr;
	playerid_t m_playerId;

	// plays the missions alone (F1 menu, CMissionMode)
	bool m_bIndividualMode = false;
	// plays with the other scripts than the local game (original vs ours, vanilla-scripts): no mission in coop with him
	bool m_bOtherScripts = false;
	// connected while the local mission (the script authority's) was already running: not part of it until it ends
	bool m_bLateForMission = false;
	// his $flag_sweet_mission_counter (the story mission Sweet's marker starts next), -1 until he tells (CMissionMode)
	int m_nSweetProgress = -1;
	uint32_t m_nStoryLaunchers = 0;  // PlayerMissionMode::storyLaunchers: his launchers alive (CMissionMode::ReadLocalLaunchers)
	// his counter of every story chain (CMissionMode, Sweet first), 255 until he tells
	uint8_t m_anChainProgress[Packets::Scripts::PlayerMissionMode::STORY_CHAIN_COUNT];
	// the id of the mission he runs, -1 when none (CVsRace: two players in the same race are a VS battle)
	int m_nRunningMission = -1;

	// reusable [0; MAX_SERVER_PLAYERS) index for the game player arrays, unlike `m_playerId`
	int m_nSlot = -1;

	Packets::Players::OnFootUpdate m_onFootSnapshotInterpolated{};
	
	uint32_t m_startedInterpolatingCameraAt = 0;
	Packets::Players::PlayerCameraSync m_cameraSnapshotOld{};
	Packets::Players::PlayerCameraSync m_cameraSnapshot{};
	
	signed short m_oShockButtonL;
	signed short m_lShockButtonL;

	Packets::Players::PlayerPlaceWaypoint m_waypointState{};

	char m_Name[Config::MAX_NICKNAME_LENGTH + 1] = { 0 };
	uint16_t m_nRTT = 0;
	// PLAYER_MONEY: shown in the TAB list only
	int32_t m_nMoney = 0;
	bool m_bHasMoney = false;

	CControllerState m_oldControllerState{};
	CControllerState m_newControllerState{};
	// the pad as it is, also while the player's controls are disabled (read by CNetworkPadSync)
	CControllerState m_rawOldControllerState{};
	CControllerState m_rawNewControllerState{};
	void SetKeys(const Packets::Players::SKeySnapshot& keys);

	CNetworkPlayerStats m_stats{};
	CPedClothesDesc m_pPedClothesDesc{};
	bool m_bHasBeenConnectedBeforeMe = false;

	bool m_bExtrapolating = false;

	// smooth on-foot error correction (smooth-onfoot)
	CVector m_vecPosError{};
	bool m_bHasPosError = false;

	bool m_bRequestedDuckTask = false;

	bool m_bIsHost = false;

	// the player is dead/arrested on his own client: the remote ped must keep the death pose and must not be
	// warped back into the vehicle by the driver/passenger updates still in flight
	bool m_bDead = false;
	uint32_t m_deadSince = 0;
	// the death was an arrest (the ped is alive with the hands up): never refreshed
	bool m_bBusted = false;

	// last state the network said about him (any update kind), used to detect a copy that looks dead but is not
	uint32_t m_netAliveTick = 0;
	int m_netHealth = 0;
	int m_netArmour = 0;
	bool m_bNetOnFoot = false;
	CVector m_vecNetFootPos{};
	Packets::Players::SWeaponSnapshot m_netWeapon{};
	// since when the copy looks dead while the network says alive (0 = it does not)
	uint32_t m_looksDeadSince = 0;

	void NoteNetState(int health, int armour, const Packets::Players::SWeaponSnapshot* weapon, const CVector* footPos);
	bool LooksDeadButAlive() const;
	// rebuilds the copy from the last network state; false when it is not safe/needed
	bool Refresh(bool force);

	~CNetworkPlayer();
	CNetworkPlayer(playerid_t id, CVector position);

	bool CreatePed(CVector position);
	void DestroyPed();
	void Respawn(const Packets::Players::RespawnPlayer& packet);
	void HandleDeath(const Packets::Players::PlayerDeath& packet);
	int GetInternalId();
	std::string GetName();
	char GetWeaponSkill(eWeaponType weaponType);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void RemoveFromVehicle(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void EnterVehiclePassenger(CVehicle* vehicle, int seatid);
	void HandleTask(Packets::Players::SetPlayerTask& packet);
	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);
};

