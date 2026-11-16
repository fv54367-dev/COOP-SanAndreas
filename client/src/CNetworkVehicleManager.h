#pragma once
class CNetworkVehicleManager
{
public:
    static inline std::vector<CNetworkVehicle*> m_pVehicles;

    static inline uint32_t ms_nNextVehicleId = 1;

    static CNetworkVehicle* GetVehicle(vehicleid_t vehicleid);
    static CNetworkVehicle* GetVehicle(CEntity* vehicle);
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static void UpdateDriver(CVehicle* pVehicle);
    static void UpdateIdle();
    static void UpdateDoors();
    // applies m_anRecvDoorRatios to a remote vehicle; false (still pending) while a ped uses its doors here
    static bool ApplyRemoteDoors(CNetworkVehicle* pNetworkVehicle);
    static void UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed);
    static vehicleid_t AllocateVehicleId();
    static void ResetIdAllocator();
    static void RemoveHostedUnused();
    static void UpdateDamageSync();

    // vehicle upgrades received before their model was loaded
    static void AddPendingComponent(vehicleid_t vehicleid, int componentId);
    static void ProcessPendingComponents();
};

