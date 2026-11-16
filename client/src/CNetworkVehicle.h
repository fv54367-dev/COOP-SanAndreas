#pragma once
class CNetworkVehicle
{
private:
	CNetworkVehicle() {}
public:
	vehicleid_t m_vehicleId{};
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	// last driver radio station a local passenger tuned to for this vehicle (-2 = none yet)
	int8_t m_lastDriverRadioStation = -2;
	// a remote mission vehicle overlapping our own individual mission vehicle (collision disabled)
	bool m_bGhost = false;
	// forced hydraulics of a remote lowrider (VehicleHydraulicSync): applied while the vehicle processes
	bool m_bHasHydraulicState = false;
	uint16_t m_hydraulicAngle = 0;
	int16_t m_hydraulicGunLeftRight = 0;
	int16_t m_hydraulicGunUpDown = 0;
	bool m_bHydraulicJump = false;
	// last door ratios sent for this vehicle (VEHICLE_DOORS), 255 = fully open
	uint8_t m_anSentDoorRatios[6]{};
	bool m_bHasSentDoorRatios = false;
	// last VEHICLE_DOORS received for a remote vehicle: kept pending while a ped enters / leaves it on this client
	// (its own door animation must not be fought), re-applied from UpdateIdle
	uint8_t m_anRecvDoorRatios[6]{};
	bool m_bDoorsPending = false;
	unsigned char m_nCreatedBy;
	int m_nBlipHandle = -1;
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	// a heli copy with an NPC driver or none (police, news): its owner says the rotor is turning (planeGearState bit
	// of PED_DRIVER_UPDATE / VEHICLE_IDLE_UPDATE), see VehicleHooks
	bool m_bRemoteRotorOn = false;
	// the bit a heli's owner sends: the rotor speed, not only bEngineOn (unreliable while flying, testeo 14-11)
	static bool IsHeliRotorTurning(CVehicle* pVehicle)
	{
		return pVehicle->m_nVehicleFlags.bEngineOn || *(float*)((uintptr_t)pVehicle + 0x84C) > 0.05f;  // m_fHeliRotorSpeed
	}
	// the searchlight CHeli::PreRender (0x6C5420) draws: m_bSearchLightEnabled (+0xA11) and m_fSearchLightIntensity (+0xA00)
	bool m_bRemoteSearchLight = false;
	static bool IsHeliSearchLightOn(CVehicle* pVehicle)
	{
		return *(uint8_t*)((uintptr_t)pVehicle + 0xA11) != 0 && *(float*)((uintptr_t)pVehicle + 0xA00) > 0.0f;
	}
	// planeGearState of a heli on the wire: 1 = rotor turning, +2 = searchlight on (older games read only > 0 as the rotor)
	static float HeliWireState(CVehicle* pVehicle)
	{
		return (IsHeliRotorTurning(pVehicle) ? 1.0f : 0.0f) + (IsHeliSearchLightOn(pVehicle) ? 2.0f : 0.0f);
	}
	CDamageManager m_oldDamageState{};

	// smooth error correction for remote vehicles (see ApplyNetworkPosition / ProcessPositionError)
	CVector m_vecPosError{};
	bool m_bHasPosError = false;
	CVector m_vecRightError{};
	CVector m_vecForwardError{};
	bool m_bHasRotError = false;

	// stream in/out (see CStreamingSync): a remote ambient vehicle far away keeps only this record
	bool m_bStreamedOut = false;
	CVector m_vecLastPos{};
	float m_fLastHeading = 0.0f;
	unsigned char m_nLastColor1 = 0;
	unsigned char m_nLastColor2 = 0;
	float m_fLastHealth = 1000.0f;
	bool m_bHasDamageState = false;
	std::vector<int> m_aComponents;
	void DestroyGameVehicle();
	void StreamOut();
	bool StreamIn();
	// B2 handover: let the game destroy the vehicle but keep the record as a remote streamed out copy, and tell the
	// server to give it to another player (see CConfigLoader::ms_bHandover / WorldHooks)
	void Release();

	~CNetworkVehicle();
	CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy, bool createNow = true);
	bool CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool HasDriver();
	void ApplyNetworkPosition(const CVector& target);
	// right = matrix right (x axis), forward = matrix "up" in plugin-sdk naming (y axis)
	void ApplyNetworkRotation(const CVector& right, const CVector& forward);
	void ProcessPositionError();
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);
};

