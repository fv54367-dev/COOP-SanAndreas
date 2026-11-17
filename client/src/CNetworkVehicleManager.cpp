#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CTrainSync.h>
#include <CAutomobile.h>
#include <CKeySync.h>
#include <COpCodeSync.h>
#include "CJackSync.h"
#include "CNetworkVehicleManager.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CSaveMenu.h"
#include "CNetwork.h"

// a mission vehicle owned by this client (created by its own SCM), not a remote copy
static bool IsLocalMissionVehicle(CVehicle* vehicle)
{
	CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
	return networkVehicle == nullptr || networkVehicle->m_bSyncing;
}

static bool IsLocalMissionVehicleNear(const CVector& pos, float radius)
{
	for (auto vehicle : CPools::ms_pVehiclePool)
	{
		if (!vehicle || vehicle->m_nCreatedBy != MISSION_VEHICLE || !IsLocalMissionVehicle(vehicle))
			continue;

		if (DistanceBetweenPoints(vehicle->GetPosition(), pos) <= radius)
			return true;
	}

	return false;
}

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(vehicleid_t vehicleid)
{
	if (!vehicleid.valid())
		return nullptr;

	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_vehicleId == vehicleid)
		{
			return m_pVehicles[i];
		}
	}
	return nullptr;
}
CNetworkVehicle* CNetworkVehicleManager::GetVehicle(CEntity* vehicle)
{
	// streamed out vehicles have no game vehicle, never match them with a null entity
	if (vehicle == nullptr)
		return nullptr;

	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_pVehicle == vehicle)
		{
			return m_pVehicles[i];
		}
	}

	return nullptr;
}

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
	CNetworkVehicleManager::m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
	auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
	if (it != m_pVehicles.end())
	{
		m_pVehicles.erase(it);
	}
}

void CNetworkVehicleManager::UpdateDriver(CVehicle* pVehicle)
{
	if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
	{
		// being dragged out: our own driver sync must not take the seat back from the jacker while the drag plays
		if (CJackSync::IsWarpSuppressed(CNetworkPlayerManager::m_localPlayerId))
			return;

		Packets::Vehicles::VehicleDriverUpdate vehicleDriverUpdate{};
		CPlayerPed* pPlayerPed = FindPlayerPed(0);
		if (pPlayerPed == nullptr || pVehicle->m_matrix == nullptr)
			return;

		vehicleDriverUpdate.vehicleid = pNetworkVehicle->m_vehicleId;
		vehicleDriverUpdate.radioStation = AudioEngine.IsVehicleRadioActive() ? AudioEngine.GetCurrentRadioStationID() : -1;
		if (pVehicle->m_pTrailer)
		{
			if (auto pNetworkTrailer = CNetworkVehicleManager::GetVehicle(pVehicle->m_pTrailer))
				vehicleDriverUpdate.trailerid = pNetworkTrailer->m_vehicleId;
		}
		vehicleDriverUpdate.pos = pVehicle->m_matrix->pos;
		vehicleDriverUpdate.roll = pVehicle->m_matrix->right;
		vehicleDriverUpdate.rot = pVehicle->m_matrix->up;
		vehicleDriverUpdate.velocity = pVehicle->m_vecMoveSpeed;
		vehicleDriverUpdate.turnSpeed = pVehicle->m_vecTurnSpeed;

		CWeapon& weapon = pPlayerPed->GetWeapon();
		vehicleDriverUpdate.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		vehicleDriverUpdate.playerWeapon.iWeaponState = weapon.m_nState;
		vehicleDriverUpdate.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		vehicleDriverUpdate.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		vehicleDriverUpdate.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		CKeySync::CollectState(vehicleDriverUpdate.playerKeys);

		vehicleDriverUpdate.color1 = pVehicle->m_nPrimaryColor;
		vehicleDriverUpdate.color2 = pVehicle->m_nSecondaryColor;

		vehicleDriverUpdate.health = pVehicle->m_fHealth;

		vehicleDriverUpdate.paintjob = pVehicle->GetRemapIndex();

		if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
		{
			CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
			vehicleDriverUpdate.miscComponentAngle = pAutomobile->m_wMiscComponentAngle;
		}

		if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
		{
			CBike* pBike = (CBike*)pVehicle;
			vehicleDriverUpdate.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
		}

		if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
		{
			CPlane* pPlane = (CPlane*)pVehicle;
			vehicleDriverUpdate.planeGearState = pPlane->m_fLandingGearStatus;
		}
		else if (pVehicle->m_nVehicleSubType == VEHICLE_HELI)
		{
			// one bit on the wire: for a heli it says the rotor is turning (the others spin theirs up, see VehicleHooks)
			vehicleDriverUpdate.planeGearState = CNetworkVehicle::HeliWireState(pVehicle);
		}

		vehicleDriverUpdate.locked = pVehicle->m_eDoorLock;

		// horn / siren / lights of a player driven car are applied by the others like a ped driver's ones
		vehicleDriverUpdate.horn = pVehicle->m_nHornCounter != 0;
		vehicleDriverUpdate.siren = pVehicle->m_nVehicleFlags.bSirenOrAlarm;
		vehicleDriverUpdate.lights = pVehicle->m_nVehicleFlags.bLightsOn;

		GetPacketFactory().Send(vehicleDriverUpdate);
	}
}

// mission-car-overlap-dedupe: during the host's mission a remote mission car copy may land on an empty ambient car this
// game made on its own (the host cleared his one with the script's CLEAR_AREA, this one stays): the twin goes
static void RemoveAmbientCarsUnderMissionCopies()
{
	static DWORD nLastCheck = 0;
	const DWORD now = GetTickCount();
	if (!CConfigLoader::ms_bMissionCarOverlapDedupe || !CNetwork::m_bConnected || !CSaveMenu::ms_bHostOnMission ||
		CCoopMission::IsScriptAuthority() || COpCodeSync::IsLocalMissionRunning() || now - nLastCheck < 500)
		return;
	nLastCheck = now;

	CPlayerPed* pLocalPlayer = FindPlayerPed(0);
	CVehicle* pLocalVehicle = pLocalPlayer ? pLocalPlayer->m_pVehicle : nullptr;
	std::vector<CVehicle*> twins;  // removed after the scan: removing erases from m_pVehicles
	for (auto copy : CNetworkVehicleManager::m_pVehicles)
	{
		CVehicle* pCopy = copy->m_pVehicle;
		if (copy->m_bSyncing || copy->m_nCreatedBy != MISSION_VEHICLE || pCopy == nullptr || !pCopy->IsVTableValid() ||
			pCopy->m_matrix == nullptr)
			continue;
		for (auto own : CNetworkVehicleManager::m_pVehicles)
		{
			CVehicle* pOwn = own->m_pVehicle;
			if (!own->m_bSyncing || pOwn == nullptr || pOwn == pLocalVehicle || !pOwn->IsVTableValid() ||
				pOwn->m_matrix == nullptr || pOwn->m_fHealth <= 0.0f ||
				(pOwn->m_nCreatedBy != RANDOM_VEHICLE && pOwn->m_nCreatedBy != PARKED_VEHICLE) ||
				pOwn->m_pDriver || pOwn->m_nNumPassengers > 0 || pOwn->m_nVehicleFlags.bHasBeenOwnedByPlayer ||
				pOwn->m_nAreaCode != pCopy->m_nAreaCode)
				continue;
			const CVector delta = pOwn->GetPosition() - pCopy->GetPosition();
			if (delta.x * delta.x + delta.y * delta.y > 1.8f * 1.8f || fabsf(delta.z) > 2.0f)
				continue;
			CMissionMirror::LogEventf("rule-car-overlap-dedupe model=%d copy=%d", pOwn->m_nModelIndex, pCopy->m_nModelIndex);
			if (std::find(twins.begin(), twins.end(), pOwn) == twins.end()) twins.push_back(pOwn);
			break;
		}
	}
	for (auto pOwn : twins)
	{
		// no health: the removal hook deletes it for everyone instead of handing the live ambient car to another player
		pOwn->m_fHealth = 0.0f;
		CWorld::Remove(pOwn);
		delete pOwn;
	}
}

void CNetworkVehicleManager::UpdateIdle()
{
	CNetworkVehicleManager::RemoveHostedUnused();
	RemoveAmbientCarsUnderMissionCopies();

	// each player runs his own copy of an individual mission: a remote mission vehicle that overlaps (<4 m) one of our
	// own mission vehicles is ghosted (no collision) until it is more than 6 m away (hysteresis)
	bool localOnlyMission = COpCodeSync::IsLocalOnlyMissionRunning();
	CPlayerPed* pLocalPlayer = FindPlayerPed(0);
	CVehicle* pLocalVehicle = (pLocalPlayer && pLocalPlayer->m_nPedFlags.bInVehicle) ? pLocalPlayer->m_pVehicle : nullptr;

	for (auto pNetworkVehicle : m_pVehicles)
	{
		CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
		if (pNetworkVehicle->m_bSyncing || pVehicle == nullptr || !pVehicle->IsVTableValid())
			continue;

		bool candidate = localOnlyMission && pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE && pVehicle != pLocalVehicle;

		if (!pNetworkVehicle->m_bGhost)
		{
			if (candidate && IsLocalMissionVehicleNear(pVehicle->GetPosition(), 4.0f))
			{
				pNetworkVehicle->m_bGhost = true;
				pVehicle->m_bUsesCollision = false;
			}
			continue;
		}

		if (candidate && IsLocalMissionVehicleNear(pVehicle->GetPosition(), 6.0f))
		{
			pVehicle->m_bUsesCollision = false;  // keep it off (a stream in recreates the game vehicle)
		}
		else
		{
			pVehicle->m_bUsesCollision = true;
			pNetworkVehicle->m_bGhost = false;
		}
	}

	// VEHICLE_DOORS that could not be applied yet (a ped was entering / leaving the car here)
	for (auto pNetworkVehicle : m_pVehicles)
	{
		if (pNetworkVehicle->m_bDoorsPending && ApplyRemoteDoors(pNetworkVehicle))
			pNetworkVehicle->m_bDoorsPending = false;
	}

	for (auto pNetworkVehicle : m_pVehicles)
	{
		CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
		if (pVehicle == nullptr || pVehicle->m_matrix == nullptr)
			continue;

		// a carriage is driven by TRAIN_STATE: no idle update (the receiver would move the copy)
		if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->HasDriver() && CTrainSync::IsSyncedCarriage(pNetworkVehicle))
			continue;

		if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->HasDriver())
		{
			Packets::Vehicles::VehicleIdleUpdate packet{};
			packet.vehicleid = pNetworkVehicle->m_vehicleId;

			packet.pos = pVehicle->m_matrix->pos;
			packet.roll = pVehicle->m_matrix->right;
			packet.rot = pVehicle->m_matrix->up;
			packet.velocity = pVehicle->m_vecMoveSpeed;
			packet.turnSpeed = pVehicle->m_vecTurnSpeed;
			packet.color1 = pVehicle->m_nPrimaryColor;
			packet.color2 = pVehicle->m_nSecondaryColor;
			packet.health = pVehicle->m_fHealth;
			packet.paintjob = pVehicle->GetRemapIndex();

			if (CUtil::GetVehicleType(pVehicle) == eVehicleType::VEHICLE_PLANE)
			{
				CPlane* plane = (CPlane*)pVehicle;
				packet.planeGearState = plane->m_fLandingGearStatus;
			}
			else if (pVehicle->m_nVehicleSubType == VEHICLE_HELI)
				packet.planeGearState = CNetworkVehicle::HeliWireState(pVehicle);

			packet.locked = pVehicle->m_eDoorLock;

			// an alarm / siren or the lights of an empty car must reach the other clients too
			packet.horn = pVehicle->m_nHornCounter != 0;
			packet.siren = pVehicle->m_nVehicleFlags.bSirenOrAlarm;
			packet.lights = pVehicle->m_nVehicleFlags.bLightsOn;

			GetPacketFactory().Send(packet);
		}
	}
}

void CNetworkVehicleManager::UpdateDoors()
{
	for (auto pNetworkVehicle : m_pVehicles)
	{
		if (!pNetworkVehicle->m_bSyncing)
			continue;

		CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
		if (pVehicle == nullptr || !pVehicle->IsVTableValid() || pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE)
			continue;

		uint8_t ratios[6]{};
		bool changed = false;

		for (int i = 0; i < 6; i++)
		{
			float openRatio = std::clamp(pVehicle->GetDooorAngleOpenRatio((eDoors)i), 0.0f, 1.0f);
			ratios[i] = (uint8_t)(openRatio * 255.0f + 0.5f);

			if (!pNetworkVehicle->m_bHasSentDoorRatios)
			{
				if (ratios[i] > 12)
					changed = true;
			}
			else if (std::abs((int)ratios[i] - (int)pNetworkVehicle->m_anSentDoorRatios[i]) > 12)
			{
				changed = true;
			}
		}

		if (!pNetworkVehicle->m_bHasSentDoorRatios)
		{
			memcpy(pNetworkVehicle->m_anSentDoorRatios, ratios, sizeof(ratios));
			pNetworkVehicle->m_bHasSentDoorRatios = true;
		}

		if (!changed)
			continue;

		Packets::Vehicles::VehicleDoors packet{};
		packet.vehicleid = pNetworkVehicle->m_vehicleId;
		memcpy(packet.ratio, ratios, sizeof(ratios));
		GetPacketFactory().Send(packet);

		memcpy(pNetworkVehicle->m_anSentDoorRatios, ratios, sizeof(ratios));
	}
}

// the ped is opening / closing a car door with its own animation (entering, leaving, jacking or being dragged out)
static bool IsPedUsingCarDoor(CPed* ped)
{
	if (ped == nullptr || ped->m_pIntelligence == nullptr)
		return false;

	CTaskManager& taskManager = ped->m_pIntelligence->m_TaskMgr;

	if (CTask* simplest = taskManager.GetSimplestActiveTask())
	{
		// the private car tasks (align, open / close door, get in / out, drag out...), see eTaskType
		int type = simplest->GetTaskType();
		if (type > TASK_COMPLEX_GO_TO_CAR_DOOR_AND_STAND_STILL && type <= TASK_SIMPLE_WAIT_UNTIL_PED_OUT_CAR)
			return true;
	}

	static const int complexTasks[] = {
		TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER, TASK_COMPLEX_STEAL_CAR,
		TASK_COMPLEX_DRAG_PED_FROM_CAR, TASK_COMPLEX_LEAVE_CAR, TASK_COMPLEX_LEAVE_CAR_AND_DIE,
		TASK_COMPLEX_LEAVE_CAR_AND_FLEE, TASK_COMPLEX_LEAVE_CAR_AND_WANDER, TASK_COMPLEX_ENTER_CAR_AS_PASSENGER_TIMED,
		TASK_COMPLEX_ENTER_CAR_AS_DRIVER_TIMED, TASK_COMPLEX_LEAVE_ANY_CAR, TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT,
		TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT,
	};

	for (int type : complexTasks)
	{
		if (taskManager.FindActiveTaskByType(type))
			return true;
	}

	return false;
}

static bool IsAnyPedUsingDoorsOf(CVehicle* vehicle)
{
	if (IsPedUsingCarDoor(vehicle->m_pDriver))
		return true;

	for (int i = 0; i < 8; i++)
	{
		if (IsPedUsingCarDoor(vehicle->m_apPassengers[i]))
			return true;
	}

	const CVector& vehiclePos = vehicle->GetPosition();
	for (auto ped : CPools::ms_pPedPool)
	{
		if (!ped || DistanceBetweenPoints(ped->GetPosition(), vehiclePos) >= 4.0f)
			continue;

		if (IsPedUsingCarDoor(ped))
			return true;
	}

	return false;
}

bool CNetworkVehicleManager::ApplyRemoteDoors(CNetworkVehicle* pNetworkVehicle)
{
	CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
	if (pNetworkVehicle->m_bSyncing)
		return true;  // ours now: nothing to apply

	if (pVehicle == nullptr || !pVehicle->IsVTableValid())
		return false;  // streamed out: apply once it is back

	if (pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE)
		return true;

	// the local player drives it: his own doors win
	CPlayerPed* pLocalPlayer = FindPlayerPed(0);
	if (pLocalPlayer && pVehicle->m_pDriver == pLocalPlayer)
		return true;

	// a ped is animating a door on this client: don't fight it, retry later
	if (IsAnyPedUsingDoorsOf(pVehicle))
		return false;

	CAutomobile* pAutomobile = (CAutomobile*)pVehicle;

	static const eCarNodes doorsNodes[6] = {CAR_BONNET, CAR_BOOT, CAR_DOOR_LF, CAR_DOOR_RF, CAR_DOOR_LR, CAR_DOOR_RR};

	for (int i = 0; i < 6; i++)
	{
		eDoors door = (eDoors)i;

		// the door was torn off or this model has no such component
		if (pAutomobile->m_aCarNodes[doorsNodes[i]] == nullptr || pAutomobile->IsDoorMissing(door))
			continue;

		float targetRatio = pNetworkVehicle->m_anRecvDoorRatios[i] / 255.0f;
		if (fabsf(pAutomobile->GetDooorAngleOpenRatio(door) - targetRatio) <= 0.05f)
			continue;

		pAutomobile->OpenDoor(nullptr, doorsNodes[i], door, targetRatio, false);
	}

	return true;
}

void CNetworkVehicleManager::UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed)
{
	 if (CJackSync::IsPlayerJackInProgress(pPlayerPed))
		 return;

	 CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
	 if (pNetworkVehicle)
	 {
		 Packets::Vehicles::VehiclePassengerUpdate packet{};

		 CWeapon& weapon = pPlayerPed->GetWeapon();
		 packet.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		 packet.playerWeapon.iWeaponState = weapon.m_nState;
		 packet.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		 packet.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		 packet.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		 CKeySync::CollectState(packet.playerKeys);

		 packet.vehicleid = pNetworkVehicle->m_vehicleId;
		 packet.driveby = CDriveBy::IsPedInDriveby(pPlayerPed);

		 // -1 until found: sending the default 0 would claim the front passenger seat for a ped that is still
		 // walking in (or that the game put in a seat we cannot see)
		 packet.seatid = -1;
		 int maxPassengers = std::min<int>(pVehicle->m_nMaxPassengers, 8);

		 for (int i = 0; i < maxPassengers; i++)
		 {
			 if (pVehicle->m_apPassengers[i] == pPlayerPed)
			 {
				 packet.seatid = i;
				 break;
			 }
		 }

		 if (packet.seatid < 0)
			 return;  // not in a passenger seat yet: don't send (the next tick will find it)

		 GetPacketFactory().Send(packet);
	 }
}

vehicleid_t CNetworkVehicleManager::AllocateVehicleId()
{
	COOP_VERIFY(CNetworkPlayerManager::m_localPlayerId.valid());
	return vehicleid_t(CNetworkPlayerManager::m_localPlayerId, ms_nNextVehicleId++);
}

void CNetworkVehicleManager::ResetIdAllocator()
{
	ms_nNextVehicleId = 1;
}

void CNetworkVehicleManager::RemoveHostedUnused()
{
	for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
	{
		if ((*it)->m_bSyncing)
		{
			CVehicle* vehicle = (*it)->m_pVehicle;
			if (!IsVehiclePointerValid(vehicle))
			{
				delete* it;
				it = m_pVehicles.erase(it);
				continue;
			}
		}
		++it;
	}
}

struct SPendingComponent
{
    vehicleid_t vehicleid;
    int componentId;
    uint32_t requestedAt;
};
static std::vector<SPendingComponent> ms_pendingComponents;

void CNetworkVehicleManager::AddPendingComponent(vehicleid_t vehicleid, int componentId)
{
    ms_pendingComponents.push_back({vehicleid, componentId, GetTickCount()});
}

void CNetworkVehicleManager::ProcessPendingComponents()
{
    for (auto it = ms_pendingComponents.begin(); it != ms_pendingComponents.end();)
    {
        bool done = GetTickCount() - it->requestedAt > 5000;  // give up, the model never came

        if (!done && CStreaming::HasVehicleUpgradeLoaded(it->componentId))
        {
            CNetworkVehicle* pNetworkVehicle = GetVehicle(it->vehicleid);
            CVehicle* pVehicle = pNetworkVehicle ? pNetworkVehicle->m_pVehicle : nullptr;
            if (pVehicle && pVehicle->IsVTableValid())
                pVehicle->AddVehicleUpgrade(it->componentId);
            done = true;
        }

        it = done ? ms_pendingComponents.erase(it) : it + 1;
    }
}

void CNetworkVehicleManager::UpdateDamageSync()
{
	for (auto pNetworkVehicle : m_pVehicles)
	{
		if (pNetworkVehicle->m_bSyncing)
		{
			CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
			if (pVehicle && pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
			{
				CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
				if (pNetworkVehicle->m_oldDamageState != pAutomobile->m_damageManager)
				{
					pNetworkVehicle->m_oldDamageState = pAutomobile->m_damageManager;
					Packets::Vehicles::VehicleDamage packet{};
					packet.vehicleid = pNetworkVehicle->m_vehicleId;
					packet.damageManager = pAutomobile->m_damageManager;
					GetPacketFactory().Send(packet);
				}
			}
		}
	}
}