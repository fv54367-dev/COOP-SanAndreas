#pragma once
class CAnimBlendAssociation;

class CNetworkPed
{
private:
	CNetworkPed() {}
public:
	// D npc-named-anim-sync: an animation state whose block is still loading is retried for this long
	static constexpr uint32_t PENDING_TASK_STATE_TIMEOUT = 5000;

	pedid_t m_pedId{};
	CPed* m_pPed = nullptr;
	bool m_bSyncing = false;
	ePedType m_nPedType = PED_TYPE_CIVMALE;
	unsigned char m_nCreatedBy;
    bool m_bInteriorPopulation = false;
    bool m_bAdoptedServicePed = false;  // m_pPed belongs to a native service script (CInteriorOwnership): never deleted here
	CVector m_vecVelocity{0.0f, 0.0f, 0.0f};

	// smooth on-foot error correction (smooth-onfoot)
	CVector m_vecPosError{};
	bool m_bHasPosError = false;
	float m_fAimingRotation = 0.0f;
	float m_fCurrentRotation = 0.0f;
	float m_fLookDirection = 0.0f;
	eMoveState m_nMoveState = eMoveState::PEDMOVE_NONE;
	float m_fMoveBlendRatio = 0.0f;
	CAutoPilot m_autoPilot;
	float m_fGasPedal = 0.0f;
	float m_fBreakPedal = 0.0f;
	float m_fSteerAngle = 0.0f;
	float m_fHealth = 100.0f;
	int m_nBlipHandle = -1;
	bool m_bClaimOnRelease = false;

	// stream in/out (see CStreamingSync): a remote ambient ped far away keeps only this record
	int m_nModelId = 0;
	int m_nLocalModelId = -1;
	uint32_t m_nCopyRetryAt = 0;
	std::vector<std::string> m_aCopyReasons;
	char m_szSpecialModelName[8]{};
	CVector m_vecLastPos{};
	// last position the network said (PED_ONFOOT) and when, for the debug export (sa_debug_json)
	CVector m_vecNetPos{};
	uint32_t m_nNetPosTick = 0;
	bool m_bStreamedOut = false;

	// interior duplicates (see CStreamingSync): the game ped was destroyed because every player sees the population
	// of his own interior; the record still exists but the stream in must not create it again while the condition holds
	bool m_bSuppressedDuplicate = false;
	unsigned char m_nAreaCode = 0;  // last known interior area (0 = outside or unknown)

	// task / animation sync (npc-task-sync)
	uint8_t m_nTaskSerial = 0;  // last serial sent (hosted) or received (remote copy)
	bool m_bHasTaskSerial = false;
	// hosted: last state sent, to only send changes
	bool m_bHasSentTaskState = false;
	uint16_t m_nLastSentTaskType = 0;
	bool m_bLastSentHasVehicle = false;
	vehicleid_t m_nLastSentVehicleId{};
	int8_t m_nLastSentSeat = -1;
	int8_t m_nLastSentDoor = 0;
	bool m_bLastSentHasAnim = false;
	uint16_t m_nLastSentAnimGroup = 0;
	uint16_t m_nLastSentAnimId = 0;
	// D npc-named-anim-sync: last state sent (hosted), to detect a change of the named animation
	bool m_bLastSentNamedAnim = false;
	uint32_t m_nLastSentAnimHash = 0;
	uint32_t m_nLastTaskStateSentAt = 0;
	uint32_t m_nInteriorDiagAt = 0;  // interior-population-sync: last pose diagnosis of this ped (both sides)
	// remote copy: the association we blended ourselves (validated against the clump before touching it)
	CAnimBlendAssociation* m_pSyncedAnim = nullptr;
	uint16_t m_nSyncedAnimGroup = 0;
	uint16_t m_nSyncedAnimId = 0;
	// remote copy: the synced animation is a named one (CAnimManager::BlendAnimation by hierarchy)
	bool m_bSyncedAnimNamed = false;
	uint32_t m_nSyncedAnimHash = 0;
	// remote copy: anim block we keep referenced while the association plays (-1 if we hold no reference)
	int m_nSyncedAnimBlock = -1;
	// D npc-named-anim-sync: last task / animation state of a remote copy, kept so it can be applied again when the
	// copy streams back in (bug 3) or when its animation block finishes loading (bug 2)
	Packets::Peds::PedTaskState m_lastTaskState{};
	bool m_bHasLastTaskState = false;
	bool m_bPendingTaskState = false;
	uint32_t m_nPendingTaskStateUntil = 0;
	// D interior handover: the named animation a ped handed over to us keeps playing while we host it
	CAnimBlendAssociation* m_pHostedAnim = nullptr;
	uint32_t m_nHostedAnimHash = 0;
	int m_nHostedAnimBlock = -1;
	// remote copy: while the enter / leave car task runs, the position sync must not move it (max 5 s)
	uint32_t m_nSkipPosSyncUntil = 0;
	bool m_bTaskSyncExpectInside = false;
	// ped-damage-authority (remote copy): the local player hurt it, its syncer has not answered yet
	uint32_t m_nLocalDamageUntil = 0;
	// ped-damage-authority (remote copy): dead here while its syncer keeps it alive, since this time
	uint32_t m_nDeadMismatchSince = 0;
	// ped-damage-authority (remote copy): when its death was asked through a damage event (0 = not asked)
	uint32_t m_nDeathRequestedAt = 0;
	// npc-aim-sync (remote copy): what its syncer does with its gun
	bool m_bAiming = false;
	CVector m_vecAimTarget{};
	bool m_bFireRequested = false;  // a PED_SHOT_SYNC arrived: the gun task plays one shot
	uint32_t m_nDriveByUntil = 0;  // npc-driveby-sync: a shot from a vehicle keeps the copy in its drive-by
	bool m_bCopyDriveBy = false;  // npc-driveby-sync: the drive-by task of the copy is ours
	uint32_t m_nShotAimUntil = 0;  // a shot keeps the copy aiming for a moment even if it was not
	// npc-anim-mirror (remote copy): anim blocks referenced while the copy plays animations from them
	std::vector<int> m_aMirrorAnimBlocks;

	bool CreateGamePed(CVector pos);
	void DestroyGamePed(const char* reason = "destroy");
	void LogCopyReason(const char* reason, int slot = -1);
	void ValidateCopyModel();
	void StreamOut();
	bool StreamIn();
	// B2 handover: let the game destroy the ped but keep the record as a remote streamed out copy, and tell the
	// server to give it to another player (see CConfigLoader::ms_bHandover / WorldHooks)
	void Release();
	void SuppressDuplicate(unsigned char areaCode = 0);

	void UpdateTaskSync();
	void ApplyTaskState(Packets::Peds::PedTaskState& packet);
	// D npc-named-anim-sync: apply the last state kept in the record (a copy that just streamed in)
	void ApplySavedTaskState();
	// D npc-named-anim-sync: retry a state whose animation block is still loading (CNetworkPedManager::Process)
	void RetryPendingTaskState();
	// D interior handover: keep the last named animation of a take-over ped playing in loop
	bool PlayHostedNamedAnim(Packets::Peds::PedTaskState& state);
	void StopHostedAnim();
	void ReleaseHostedAnimBlock();
	// apply a task state to the game ped (false: its animation block is still loading)
	bool ApplyTaskStateInternal(Packets::Peds::PedTaskState& packet, bool applyTask = true);
	bool ApplyNamedAnimState(Packets::Peds::PedTaskState& packet);
	bool IsPosSyncSkipped();
	void StopSyncedAnimation();
	void ReleaseSyncedAnimBlock();
	// a copy always keeps one full-body animation, given back the game's way (CNetworkPedManager::Process)
	void EnsureBaseAnimation();
	// the syncer sent health 0: the copy dies the game's way (CTaskComplexDie) instead of standing with no health
	void ApplyRemoteDeath();
	// health sent by the syncer: kept in the record and given to the copy (see CPedDamageSync)
	void ApplyAuthorityHealth(float health);
	// npc-aim-sync: every frame (CNetworkPedManager::Process)
	void ProcessAim();
	void DiagnoseInteriorCopy();
	void ProcessDriveBy();
	// npc-anim-mirror: hosted, send the animations playing now; copy, play exactly that set
	void SendAnimSet();
	void ApplyAnimSet(Packets::Peds::PedAnimSet& packet);
	void ReleaseMirrorAnimBlocks();

	static CNetworkPed* CreateHosted(CPed* ped);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	// bWarpOut false: use the game's own exit animation (a player pulled it out) instead of the raw warp
	void RemoveFromVehicle(CVehicle* vehicle, bool bWarpOut = true);
	void ClaimOnRelease();
	void CancelClaim();

	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);

	CNetworkPed(pedid_t pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[], bool createNow = true);
	~CNetworkPed();
};

