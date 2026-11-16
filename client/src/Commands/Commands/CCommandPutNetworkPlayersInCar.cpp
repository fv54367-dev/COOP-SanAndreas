#include "stdafx.h"
#include "CCommandPutNetworkPlayersInCar.h"
#include "CCommandTeleportPlayersToHostSafely.h"
#include <CCoopMission.h>
#include <CConfigLoader.h>
#include <CMissionMirror.h>
#include <CCarEnterExit.h>
#include <CMissionSyncRules.h>
#include <CMirrorBlips.h>
#include <COpCodeSync.h>

static constexpr int FALLBACK_MAX_PLAYERS = 3;
static constexpr int MAX_PASSENGER_SEATS = 8;
static constexpr float MIN_CAR_DISTANCE = 5.0f;
static constexpr float BOARDING_WALK_RADIUS = 25.0f;
static constexpr int FOUR_DOOR_GUEST_MODEL = 492; // Greenwood: guests' shared car when the host's has two doors
// mission boarding (mission-boarding-shared-car): only these guests, the ones the host's car had no seat for
static const std::vector<playerid_t>* s_pOnlyGuests = nullptr;
// a mission rule already parked the guests' vehicles (Drive-Thru, OG Loc...): boarding adds none
static bool HasParkedCopies();

// a free spot behind the host car: 7 m, then 13 m, ... alternating a little to the sides
static CVector FindFreeCarPosition(CVehicle* pVehicle, const std::vector<CVector>& usedPositions)
{
	const CVector forward = pVehicle->GetForward();
	const CVector right = pVehicle->GetRight();
	const CVector origin = pVehicle->GetPosition();

	// nothing better found: right next to the host car, at its level
	CVector lastPos = origin + right * 3.0f;

	for (int k = 0; k < 8; k++)
	{
		const float distance = 7.0f + 6.0f * (float)k;
		for (int side = -1; side <= 1; side++)
		{
			CVector pos = origin - forward * distance + right * ((float)side * 3.0f);

			// the ground under the host car's level: searching from high above finds roofs
			bool foundGround = false;
			const float groundZ = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, origin.z + 3.0f, &foundGround, nullptr);
			if (!foundGround || fabsf(groundZ - origin.z) > 3.0f)
				continue;
			pos.z = groundZ;
			lastPos = pos;

			bool bFree = true;
			for (const CVector& used : usedPositions)
			{
				if (DistanceBetweenPoints(pos, used) < MIN_CAR_DISTANCE)
				{
					bFree = false;
					break;
				}
			}
			if (!bFree)
				continue;

			// ignore the host car itself, we are placing behind it
			if (CWorld::TestSphereAgainstWorld(pos, 3.0f, pVehicle, true, true, true, true, false, false))
				continue;

			return pos;
		}
	}

	return lastPos;
}

// PED_MISSION = 2; also exclude network players represented by ordinary peds.
static bool IsMissionNpc(CPed* ped)
{
	return ped && !ped->IsPlayer() && !CNetworkPlayerManager::GetPlayer(ped) && ped->m_nCreatedBy == 2;
}

static bool ReserveMissionPassengers(CVehicle* pVehicle, bool (&reservedSeats)[MAX_PASSENGER_SEATS])
{
	bool found = false;
	if (CPools::ms_pPedPool)
	{
		// Native target seats are door IDs (gta-reversed, 0x640340 / 0x64F1E0).
		for (int i = 0; i < CPools::ms_pPedPool->m_nSize; i++)
		{
			CPed* ped = CPools::ms_pPedPool->GetAt(i);
			if (!IsMissionNpc(ped) ||
				ped->m_nPedFlags.bInVehicle || !ped->m_pIntelligence)
				continue;
			for (auto primary : ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks)
			{
				for (CTask* task = primary; task; task = task->GetSubTask())
				{
					if (task->GetTaskType() != TASK_COMPLEX_ENTER_CAR_AS_PASSENGER &&
						task->GetTaskType() != TASK_COMPLEX_ENTER_CAR_AS_DRIVER)
						continue;
					auto enter = static_cast<CTaskComplexEnterCar*>(task);
					if (enter->m_pTargetVehicle != pVehicle)
						continue;
					found = true;
					if (task->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_DRIVER)
						continue;
					const int seat = CCarEnterExit::ComputePassengerIndexFromCarDoor(pVehicle,
						enter->m_nTargetSeat ? enter->m_nTargetSeat : enter->m_nTargetDoor);
					if (seat >= 0 && seat < MAX_PASSENGER_SEATS)
						reservedSeats[seat] = true;
					else
						for (bool& reserved : reservedSeats) reserved = true;
				}
			}
		}
	}

	return found;
}

bool CCommandPutNetworkPlayersInCar::HasMissionPassenger(CVehicle* vehicle, bool includeEntering)
{
    if (!vehicle)
        return false;
    if (IsMissionNpc(vehicle->m_pDriver))
        return true;
    for (auto passenger : vehicle->m_apPassengers)
        if (IsMissionNpc(passenger))
            return true;
    bool reserved[MAX_PASSENGER_SEATS] = {};
    return includeEntering && ReserveMissionPassengers(vehicle, reserved);
}

void CCommandPutNetworkPlayersInCar::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	if (!CCoopMission::IsScriptAuthority())
		return;

	// The polling rule owns this entry, including the script's backup warp.
	CVehicle* vehicle = CPools::GetVehicle(ScriptParams[0]);
	CPlayerPed* player = FindPlayerPed(0);
	if (CConfigLoader::ms_bMissionSyncRules && CConfigLoader::ms_bMissionScriptedBoarding &&
		CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && script->m_bIsMission &&
		CMissionSyncRules::IsStoryScript(script) && COpCodeSync::IsScriptSynced(script) &&
		!CMirrorBlips::IsPerPlayerMissionRunning() && player && player->m_nPedFlags.bInVehicle && player->m_pVehicle == vehicle && vehicle)
	{
		if (HasMissionPassenger(vehicle, true))
			return;
	}
	PutPlayersInCar(vehicle);
}

void CCommandPutNetworkPlayersInCar::PutPlayersInCar(CVehicle* pVehicle, bool bMissionBoarding)
{
	CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;

	// no usable networked car: same as the old teleport next to the host
	if (!pVehicle || pVehicle->m_fHealth <= 0.0f || !pNetworkVehicle)
	{
		if (bMissionBoarding)
			return;
		CNetworkPlayer* networkPlayers[FALLBACK_MAX_PLAYERS];
		int playerCount = 0;
		for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
		{
			if (playerCount < FALLBACK_MAX_PLAYERS && networkPlayer->m_pPed && CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
				networkPlayers[playerCount++] = networkPlayer;
		}
		CCommandTeleportPlayersToHostSafely::TeleportToHost(networkPlayers, playerCount);
		return;
	}

	const vehicleid_t vehicleid = pNetworkVehicle->m_vehicleId;
	const uint16_t model = (uint16_t)pVehicle->m_nModelIndex;
	const float heading = pVehicle->GetHeading() * 57.2957795f;
	const uint8_t color1 = (uint8_t)pVehicle->m_nPrimaryColor;
	const uint8_t color2 = (uint8_t)pVehicle->m_nSecondaryColor;

	bool reservedSeats[MAX_PASSENGER_SEATS] = {};
	std::vector<CVector> createdCarPositions;
	std::vector<playerid_t> leftover;
	int seats = 0, copies = 0, farPlayers = 0;
	const bool ninesFarBoarding = bMissionBoarding && CConfigLoader::ms_bMissionNinesFarBoarding &&
		CMissionMirror::GetRunningMissionId() == 16;
	// Nines and AK's, end of the practice: the guests come to the host's car and get in with the normal entry animation
	// (mission-nines-board-near), even with mission-no-auto-board and from far away
	const bool ninesBoardNear = ninesFarBoarding && CConfigLoader::ms_bMissionNinesBoardNear;
	if (bMissionBoarding)
		ReserveMissionPassengers(pVehicle, reservedSeats);

	// Allocate seats to nearby players before deciding which distant players need copies.
	auto networkPlayers = CNetworkPlayerManager::m_pPlayers;
	if (bMissionBoarding)
		std::stable_partition(networkPlayers.begin(), networkPlayers.end(), [pVehicle](CNetworkPlayer* player) {
			return player->m_pPed && DistanceBetweenPoints(player->m_pPed->GetPosition(), pVehicle->GetPosition()) < BOARDING_WALK_RADIUS;
		});
	for (auto networkPlayer : networkPlayers)
	{
		if (!networkPlayer->m_pPed || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
			continue;

		if (bMissionBoarding && (networkPlayer->m_pPed->m_nPedFlags.bInVehicle ||
			networkPlayer->m_pPed->m_fHealth <= 0.0f || networkPlayer->m_pPed->m_nAreaCode != pVehicle->m_nAreaCode))
			continue;

		Packets::Scripts::PutInVehicleScripted packet{};
		packet.playerid = networkPlayer->m_playerId;
		packet.vehicleid = vehicleid;
		packet.model = model;
		packet.heading = heading;
		packet.color1 = color1;
		packet.color2 = color2;

		int seat = -1;
		for (int i = 0; i < pVehicle->m_nMaxPassengers && i < MAX_PASSENGER_SEATS; i++)
		{
			if (!reservedSeats[i] && pVehicle->m_apPassengers[i] == nullptr)
			{
				seat = i;
				break;
			}
		}

		const bool distant = bMissionBoarding &&
			DistanceBetweenPoints(networkPlayer->m_pPed->GetPosition(), pVehicle->GetPosition()) >= BOARDING_WALK_RADIUS;
		if (distant)
		{
			farPlayers++;
			if (seat >= 0 && !ninesFarBoarding)
				continue;
		}

		// mission-no-auto-board: the seat stays free, the guest gets in by himself
		if (seat >= 0 && bMissionBoarding && CConfigLoader::ms_bMissionNoAutoBoard && !ninesBoardNear)
		{
			reservedSeats[seat] = true;
			seats++;
			continue;
		}
		if (seat >= 0)
		{
			reservedSeats[seat] = true;
			packet.seat = (int8_t)seat;
			packet.walk = bMissionBoarding && (!distant || ninesBoardNear);
			packet.pos = pVehicle->GetPosition();
			seats++;
		}
		else
		{
			if (bMissionBoarding && !CConfigLoader::ms_bMissionGuestCarCopies)
			{
				if (!distant)
					leftover.push_back(networkPlayer->m_playerId);
				continue;
			}
			packet.seat = bMissionBoarding ? PARKED_COPY_SEAT : -1;
			copies++;
			packet.pos = FindFreeCarPosition(pVehicle, createdCarPositions);
			createdCarPositions.push_back(packet.pos);
		}

		GetPacketFactory().Send(packet);
	}
	if (bMissionBoarding)
		CMissionMirror::LogEventf("rule-boarding vehicle=%d walk=%d copies=%d far=%d left=%d", model, seats, copies, farPlayers,
			(int)leftover.size());
	// the host's car is full of the mission's people: the rest share one 4-door car (the user's rule of 11.11)
	CRunningScript* mission = nullptr;
	for (auto script = CTheScripts::pActiveScripts; script && !mission; script = script->m_pNext)
		if (script->m_bIsMission && COpCodeSync::IsScriptSynced(script))
			mission = script;
	if (!leftover.empty() && CConfigLoader::ms_bMissionBoardingSharedCar && mission && !HasParkedCopies() &&
		!(CConfigLoader::ms_bMissionNoAutoBoard && MissionGuestCarMade()))
	{
		s_pOnlyGuests = &leftover;
		ParkSharedGuestCar(pVehicle, mission, "boarding", 1);
		s_pOnlyGuests = nullptr;
	}
}

// a free spot on the ground beside the vehicle, at its level: 1.4 m to the right, to the left, 2.8 m to the right...
static CVector FindParkingSpot(CVehicle* pVehicle, const std::vector<CVector>& usedPositions)
{
	const CVector right = pVehicle->GetRight();
	const CVector origin = pVehicle->GetPosition();

	for (int k = 1; k <= 8; k++)
	{
		for (int side = 1; side >= -1; side -= 2)
		{
			CVector pos = origin + right * (1.4f * (float)k * (float)side);

			bool foundGround = false;
			const float groundZ = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, origin.z + 2.0f, &foundGround, nullptr);
			if (!foundGround || fabsf(groundZ - origin.z) > 2.5f)
				continue;
			pos.z = groundZ;

			bool bFree = true;
			for (const CVector& used : usedPositions)
			{
				if (DistanceBetweenPoints(pos, used) < 1.2f)
				{
					bFree = false;
					break;
				}
			}
			if (!bFree)
				continue;

			if (CWorld::TestSphereAgainstWorld(pos + CVector(0.0f, 0.0f, 0.8f), 0.6f, pVehicle, true, true, false, true, false, false))
				continue;

			return pos;
		}
	}

	return origin + right * (1.4f * (float)(usedPositions.size() + 1));
}

int CCommandPutNetworkPlayersInCar::ParkCopiesNextTo(CVehicle* pVehicle, int guestsPerCopy)
{
	if (!pVehicle)
		return 0;

	CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
	std::vector<CVector> usedPositions;
	int sent = 0;
	int guest = 0;
	guestsPerCopy = (std::max)(1, guestsPerCopy);
	for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (!networkPlayer->m_pPed || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
			continue;

		if (guest++ % guestsPerCopy != 0)
			continue;

		Packets::Scripts::PutInVehicleScripted packet{};
		packet.playerid = networkPlayer->m_playerId;
		if (pNetworkVehicle)
			packet.vehicleid = pNetworkVehicle->m_vehicleId;
		packet.seat = PARKED_COPY_SEAT;
		packet.model = (uint16_t)pVehicle->m_nModelIndex;
		packet.pos = FindParkingSpot(pVehicle, usedPositions);
		packet.heading = pVehicle->GetHeading() * 57.2957795f;
		packet.color1 = (uint8_t)pVehicle->m_nPrimaryColor;
		packet.color2 = (uint8_t)pVehicle->m_nSecondaryColor;
		usedPositions.push_back(packet.pos);
		GetPacketFactory().Send(packet);
		sent++;
	}
	return sent;
}

// Strict placement for mission guest vehicles; no unchecked fallback.
static bool FindGuestVehiclePosition(CVehicle* vehicle, CVector& result, const float (*offsets)[2], int count, float halfLength, float radius, const CVector* avoid)
{
    const CVector origin = vehicle->GetPosition();
    // SET_CAR_COORDINATES lifts the car off the floor: compare against the floor under it, not its centre.
    bool foundBase = false;
    const float baseZ = CWorld::FindGroundZFor3DCoord(origin.x, origin.y, origin.z + 1.0f, &foundBase, nullptr);
    if (!foundBase || !std::isfinite(baseZ))
        return false;
    const CVector forward = vehicle->GetForward();
    const CVector right = vehicle->GetRight();
    for (int candidate = 0; candidate < count; ++candidate)
    {
        const auto& offset = offsets[candidate];
        CVector pos = origin - forward * offset[0] + right * offset[1];
        float minZ = baseZ + 1.0f;
        float maxZ = baseZ - 1.0f;
        bool valid = true;
        for (int end = -1; end <= 1; ++end)
        {
            const CVector sample = pos + forward * (halfLength * (float)end);
            bool found = false;
            const float z = CWorld::FindGroundZFor3DCoord(sample.x, sample.y, baseZ + 1.0f, &found, nullptr);
            if (!found || !std::isfinite(z) || fabsf(z - baseZ) >= 1.0f)
            {
                valid = false;
                break;
            }
            minZ = (std::min)(minZ, z);
            maxZ = (std::max)(maxZ, z);
            if (end == 0) pos.z = z;
        }
        if (!valid || maxZ - minZ >= 0.6f || !(DistanceBetweenPoints(pos, origin) <= 12.0f))
            continue;
        const CVector enemyDelta = avoid ? pos - *avoid : CVector(100.0f, 100.0f, 0.0f);
        if (!(enemyDelta.x * enemyDelta.x + enemyDelta.y * enemyDelta.y > 100.0f))
            continue;
        if (!CWorld::GetIsLineOfSightClear(CVector(origin.x, origin.y, baseZ + 0.8f),
            pos + CVector(0.0f, 0.0f, 0.8f), true, false, false, true, false, false, false))
            continue;
        // Overlapping spheres cover the vehicle's length and width, above the floor.
        for (int part = -5; part <= 5; ++part)
        {
            if (CWorld::TestSphereAgainstWorld(pos + forward * (halfLength * 0.2f * (float)part) + CVector(0.0f, 0.0f, 1.3f),
                radius, vehicle, true, true, true, true, false, false))
            {
                valid = false;
                break;
            }
        }
        if (valid)
        {
            result = pos;
            return true;
        }
    }
    return false;
}

struct SGuestBoarding
{
    playerid_t player;
    int vehicleRef;
    int hostRef;
    int seat;
    DWORD created;
    DWORD ready;
    bool waitForHost;
    CRunningScript* script;
    const bool* enabled;
    bool mechanic = false;  // QueueGuestPassenger: part of the mission (Just Business escort), not a lift
};
static std::vector<SGuestBoarding> guestBoarding;
static bool s_bMissionGuestCarMade = false;

bool CCommandPutNetworkPlayersInCar::MissionGuestCarMade()
{
    return s_bMissionGuestCarMade;
}

void CCommandPutNetworkPlayersInCar::ResetMissionGuestCar()
{
    s_bMissionGuestCarMade = false;
}

void CCommandPutNetworkPlayersInCar::ProcessGuestVehicles()
{
    if (COpCodeSync::bProcessingNetworkOpcode) return;
    const DWORD now = GetTickCount();
    CPlayerPed* host = FindPlayerPed(0);
    for (size_t i = 0; i < guestBoarding.size();)
    {
        auto& pending = guestBoarding[i];
        CNetworkPlayer* guest = nullptr;
        for (auto player : CNetworkPlayerManager::m_pPlayers)
            if (player->m_playerId == pending.player) guest = player;
        CVehicle* vehicle = CPools::GetVehicle(pending.vehicleRef);
        CVehicle* source = CPools::GetVehicle(pending.hostRef);
        bool active = false;
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script == pending.script && script->m_bIsMission && COpCodeSync::IsScriptSynced(script)) active = true;
        bool remove = !CNetwork::m_bConnected || !CConfigLoader::ms_bMissionSyncRules ||
            !CCoopMission::IsScriptAuthority() || !active || !*pending.enabled ||
            !host || host->m_fHealth <= 0.0f || !guest || !guest->m_pPed ||
            !CCoopMission::ShouldIncludeNetworkPlayer(guest) || guest->m_pPed->m_fHealth <= 0.0f ||
            !vehicle || !source || vehicle->m_fHealth <= 0.0f ||
            guest->m_pPed->m_nAreaCode != vehicle->m_nAreaCode;
        if (!remove && !pending.ready && (!pending.waitForHost ||
            (host->m_nPedFlags.bInVehicle && host->m_pVehicle == source))) pending.ready = now;
        remove = remove || (pending.ready ? now - pending.ready >= 8000 : now - pending.created >= 90000);
        // A free seat in the host car: drop it if a mission NPC took or is taking it meanwhile.
        if (!remove && pending.ready && vehicle == source && pending.seat >= 0)
        {
            bool reserved[MAX_PASSENGER_SEATS] = {};
            ReserveMissionPassengers(vehicle, reserved);
            if (pending.seat >= vehicle->m_nMaxPassengers || pending.seat >= MAX_PASSENGER_SEATS ||
                reserved[pending.seat] || vehicle->m_apPassengers[pending.seat])
            {
                CMissionMirror::LogEventf("rule-guest-host-seat dropped player=%u seat=%d", pending.player.value(), pending.seat);
                remove = true;
            }
        }
        if (!remove && pending.ready && !guest->m_pPed->m_nPedFlags.bInVehicle && !pending.mechanic &&
            CConfigLoader::ms_bMissionNoAutoBoard)
        {
            // the vehicle stays parked with its blip: he gets on it himself (testeo 13-11)
            CMissionMirror::LogEventf("rule-guest-board skipped player=%u seat=%d", pending.player.value(), pending.seat);
            remove = true;
        }
        if (!remove && pending.ready && !guest->m_pPed->m_nPedFlags.bInVehicle)
        {
            CNetworkVehicle* network = CNetworkVehicleManager::GetVehicle(vehicle);
            if (network && network->m_vehicleId.valid())
            {
                Packets::Scripts::PutInVehicleScripted packet{};
                packet.playerid = pending.player;
                packet.vehicleid = network->m_vehicleId;
                packet.seat = (int8_t)pending.seat;
                packet.model = 0; // Existing authority vehicle only; never create a fallback.
                GetPacketFactory().Send(packet);
                remove = true;
            }
        }
        if (remove) guestBoarding.erase(guestBoarding.begin() + i);
        else ++i;
    }
}

void CCommandPutNetworkPlayersInCar::QueueGuestPassenger(playerid_t player, CVehicle* vehicle, int seat, CRunningScript* script,
    const bool* enabled)
{
    if (!vehicle || !script || !enabled || seat < 0) return;
    const int ref = CPools::GetVehicleRef(vehicle);
    guestBoarding.push_back({player, ref, ref, seat, GetTickCount(), 0, false, script, enabled, true});
}

int CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(CVehicle* vehicle, CRunningScript* script,
    const char* rule, int perVehicle, bool waitForHost, int phase)
{
    if (!vehicle || !script || perVehicle < 1 || perVehicle > 3) return 0;
    const bool driveBy = strcmp(rule, "drive-by") == 0;
    const bool sepulcros = strcmp(rule, "los-sepulcros") == 0;
    const bool runningDog = strcmp(rule, "running-dog") == 0;
    const bool tracks = strcmp(rule, "wrong-side-tracks") == 0;
    const bool business = strcmp(rule, "just-business") == 0;
    const bool uncleSam = strcmp(rule, "robbing-uncle-sam") == 0;
    const bool maddDogg = strcmp(rule, "madd-dogg") == 0;
    const bool management = strcmp(rule, "management-issues") == 0;
    const bool greenSabre = strcmp(rule, "green-sabre") == 0;
    const bool boarding = strcmp(rule, "boarding") == 0;
    const bool countryside = strcmp(rule, "countryside") == 0;
    const bool driveThru = strcmp(rule, "drive-thru") == 0;
    const bool tripSkip = strcmp(rule, "trip-skip") == 0;
    const bool catalyst = strcmp(rule, "catalyst") == 0;
    const bool bike = perVehicle == 2 || (tracks && vehicle->m_nModelIndex == 468) ||
        (business && vehicle->m_nModelIndex == 581) || (maddDogg && vehicle->m_nModelIndex == 510);
    const bool* enabled = catalyst ? &CConfigLoader::ms_bMissionCatalystGuestCar : tripSkip ? &CConfigLoader::ms_bMissionTripSkipGuests :
        countryside ? &CConfigLoader::ms_bCoopCountryside : boarding ? &CConfigLoader::ms_bMissionBoardingSharedCar : greenSabre ? &CConfigLoader::ms_bCoopGreenSabre : maddDogg ? &CConfigLoader::ms_bCoopMaddDogg : management ? &CConfigLoader::ms_bCoopManagementIssues : uncleSam ? &CConfigLoader::ms_bCoopRobbingUncleSam : business ? &CConfigLoader::ms_bCoopJustBusiness : tracks ? &CConfigLoader::ms_bCoopWrongSideTracks :
        runningDog ? &CConfigLoader::ms_bCoopRunningDog : sepulcros ? &CConfigLoader::ms_bCoopLosSepulcros :
        driveBy ? &CConfigLoader::ms_bCoopDriveBy : waitForHost ? &CConfigLoader::ms_bMissionOgLocBikes :
        (driveThru ? &CConfigLoader::ms_bMissionDriveThruGuestCar : &CConfigLoader::ms_bMissionOgLocGuestCar);
    // Story rules: as few vehicles as possible (max 3 guests). Free host seats first, only where the
    // script creates its NPCs already seated (Smoke in the 466/Sanchez); then one shared 4-door car,
    // or bikes of two.
    const bool fewest = !waitForHost && perVehicle == 1 &&
        (driveBy || sepulcros || runningDog || tracks || business || uncleSam || management || greenSabre || boarding || countryside ||
         tripSkip || catalyst || (driveThru && CConfigLoader::ms_bMissionDriveThruFirstCar));
    const bool hostSeats = fewest && !bike && (runningDog || tracks || business || management || greenSabre) &&
        CNetworkVehicleManager::GetVehicle(vehicle);
    int model = vehicle->m_nModelIndex;
    if (fewest)
    {
        perVehicle = bike ? 2 : 3;
        if (catalyst)
            perVehicle = (std::min)(3, 1 + (int)vehicle->m_nMaxPassengers);  // the mission's own model, as many as it seats
        else if (!bike && (vehicle->m_nMaxPassengers < 3 ||
                           ((boarding || tripSkip) && vehicle->m_nVehicleSubType != VEHICLE_AUTOMOBILE)))
            model = FOUR_DOOR_GUEST_MODEL;
    }
    // T98: parking (when the mission makes the vehicle) counts every guest of the mission around; nobody is in a seat yet
    const bool parkOnly = phase == GUEST_PARK_ONLY;
    const bool board = phase == GUEST_PARK_BOARD;
    std::vector<CNetworkPlayer*> guests;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
        if (player->m_pPed && player->m_pPed->m_fHealth > 0.0f &&
            (!s_pOnlyGuests || std::find(s_pOnlyGuests->begin(), s_pOnlyGuests->end(), player->m_playerId) != s_pOnlyGuests->end()) &&
            player->m_pPed->m_nAreaCode == vehicle->m_nAreaCode && CCoopMission::ShouldIncludeNetworkPlayer(player) &&
            (!(driveBy || sepulcros || runningDog || tracks || business || uncleSam || maddDogg || management || greenSabre || boarding ||
                 countryside || tripSkip || catalyst || (driveThru && fewest)) || tripSkip || (parkOnly || board
                ? ((!player->m_pPed->m_nPedFlags.bInVehicle || (board && IsParkedCopy(player->m_pPed->m_pVehicle))) &&
                   DistanceBetweenPoints(player->m_pPed->GetPosition(), vehicle->GetPosition()) <= 300.0f)
                : (!player->m_pPed->m_nPedFlags.bInVehicle &&
                   DistanceBetweenPoints(player->m_pPed->GetPosition(), vehicle->GetPosition()) <= 120.0f))))
            guests.push_back(player);
    const float carOffsets[][2] = {{6,0},{9,0},{0,4},{0,-4},{6,4},{6,-4},{9,4},{9,-4}};
    const float bikeOffsets[][2] = {{0,3},{0,-3},{4,3},{4,-3},{7,0},{7,3},{7,-3},{10,0}};
    const CVector avoid(2411.31f, -1928.84f, 0.0f);
    int vehicles = 0, seated = 0;
    size_t first = 0;
    if (hostSeats)
    {
        bool reserved[MAX_PASSENGER_SEATS] = {};
        ReserveMissionPassengers(vehicle, reserved);
        // parked before Smoke boards: keep his seat out of the count (he rides in all three of these)
        if (parkOnly && !HasMissionPassenger(vehicle, true))
            for (int seat = 0; seat < vehicle->m_nMaxPassengers && seat < MAX_PASSENGER_SEATS; ++seat)
                if (!vehicle->m_apPassengers[seat])
                {
                    reserved[seat] = true;
                    break;
                }
        const int hostRef = CPools::GetVehicleRef(vehicle);
        for (int seat = 0; seat < vehicle->m_nMaxPassengers && seat < MAX_PASSENGER_SEATS && first < guests.size(); ++seat)
        {
            if (reserved[seat] || vehicle->m_apPassengers[seat])
                continue;
            // parking only counts the free seats: they are given when the host gets in (GUEST_HOST_SEATS_ONLY)
            if (parkOnly)
                ++first;
            else
                guestBoarding.push_back({guests[first++]->m_playerId, hostRef, hostRef, seat, GetTickCount(), 0, false, script, enabled});
            ++seated;
        }
    }
    if (phase == GUEST_HOST_SEATS_ONLY)
    {
        CMissionMirror::LogEventf("rule-guest-vehicle case=%s phase=host-seats guests=%d host-seats=%d",
            rule, (int)guests.size(), seated);
        return 0;
    }
    if (first < guests.size() && model != vehicle->m_nModelIndex &&
        CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
    {
        CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
        CStreaming::LoadAllRequestedModels(false);
    }
    // Catalyst: one extra car only (the owner's rule); the rest of the guests keep the old train ride
    for (; first < guests.size() && !(catalyst && vehicles >= 1); first += perVehicle)
    {
        CVector pos;
        if (!FindGuestVehiclePosition(vehicle, pos, bike ? bikeOffsets : carOffsets, 8,
            bike ? 1.2f : 2.5f, bike ? 0.7f : 1.2f,
            driveThru ? &avoid : nullptr)) break;
        int handle = 0;
        Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &handle);
        if (!CPools::GetVehicle(handle)) break;
        Command<Commands::SET_CAR_HEADING>(handle, vehicle->GetHeading() * 57.2957795f);
        Command<Commands::CHANGE_CAR_COLOUR>(handle, vehicle->m_nPrimaryColor, vehicle->m_nSecondaryColor);
        if (driveBy) Command<Commands::SET_CAR_HEALTH>(handle, 1450);
        AddParkedCopy(handle, script);
        ++vehicles;
        s_bMissionGuestCarMade = true;
        // T98: parked with its blip; each guest gets on it by himself
        for (size_t seat = 0; !parkOnly && seat < (size_t)perVehicle && first + seat < guests.size(); ++seat)
        {
            guestBoarding.push_back({guests[first + seat]->m_playerId, handle, CPools::GetVehicleRef(vehicle),
                seat == 0 ? GUEST_DRIVER_SEAT : (int)seat - 1, GetTickCount(), 0, waitForHost, script, enabled, board});
        }
    }
    CMissionMirror::LogEventf("rule-guest-vehicle case=%s guests=%d host-seats=%d vehicles=%d model=%d park-only=%d",
        rule, (int)guests.size(), seated, vehicles, model, parkOnly ? 1 : 0);
    return vehicles;
}

struct SParkedCopy
{
	int vehicleHandle;
	int blipHandle;
};
static std::vector<SParkedCopy> aParkedCopies;

static bool HasParkedCopies()
{
    return !aParkedCopies.empty();
}

size_t CCommandPutNetworkPlayersInCar::ParkedCopyCount()
{
    return aParkedCopies.size();
}

static void ReleaseParkedCopy(const SParkedCopy& copy)
{
    if (copy.blipHandle != -1)
        CRadar::ClearBlip(copy.blipHandle);
    if (CPools::GetVehicle(copy.vehicleHandle))
        Command<Commands::MARK_CAR_AS_NO_LONGER_NEEDED>(copy.vehicleHandle);
}

void CCommandPutNetworkPlayersInCar::ReleaseParkedCopies(size_t count)
{
    count = (std::min)(count, aParkedCopies.size());
    for (size_t i = 0; i < count; ++i)
        ReleaseParkedCopy(aParkedCopies[i]);
    aParkedCopies.erase(aParkedCopies.begin(), aParkedCopies.begin() + count);
}

void CCommandPutNetworkPlayersInCar::ReleaseParkedCopiesFar(const CVector& at, float radius, const std::vector<playerid_t>& players)
{
    for (size_t i = 0; i < aParkedCopies.size();)
    {
        CVehicle* vehicle = CPools::GetVehicle(aParkedCopies[i].vehicleHandle);
        if (vehicle && DistanceBetweenPoints(vehicle->GetPosition(), at) <= radius)
        {
            ++i;
            continue;
        }
        ReleaseParkedCopy(aParkedCopies[i]);
        aParkedCopies.erase(aParkedCopies.begin() + i);
    }
    for (size_t i = 0; i < guestBoarding.size();)
    {
        if (std::find(players.begin(), players.end(), guestBoarding[i].player) != players.end())
            guestBoarding.erase(guestBoarding.begin() + i);
        else
            ++i;
    }
}

int CCommandPutNetworkPlayersInCar::BoardGuestsAfterTripSkip(CVehicle* hostVehicle, CRunningScript* script,
    const std::vector<playerid_t>& guests)
{
    if (!hostVehicle || !script || guests.empty() || !CConfigLoader::ms_bMissionTripSkipGuests)
        return 0;
    // the seats the mission's own people hold or are walking to stay out of the count
    bool reserved[MAX_PASSENGER_SEATS] = {};
    ReserveMissionPassengers(hostVehicle, reserved);
    const int hostRef = CPools::GetVehicleRef(hostVehicle);
    std::vector<playerid_t> leftover;
    int seated = 0;
    for (const playerid_t& id : guests)
    {
        int seat = -1;
        for (int i = 0; i < hostVehicle->m_nMaxPassengers && i < MAX_PASSENGER_SEATS; ++i)
            if (!reserved[i] && !hostVehicle->m_apPassengers[i])
            {
                seat = i;
                break;
            }
        if (seat < 0)
        {
            leftover.push_back(id);
            continue;
        }
        reserved[seat] = true;
        ++seated;
        guestBoarding.push_back({id, hostRef, hostRef, seat, GetTickCount(), 0, false, script,
            &CConfigLoader::ms_bMissionTripSkipGuests, true});
    }
    int vehicles = 0;
    if (!leftover.empty())
    {
        s_pOnlyGuests = &leftover;
        vehicles = ParkSharedGuestCar(hostVehicle, script, "trip-skip", 1, false, GUEST_PARK_BOARD);
        s_pOnlyGuests = nullptr;
    }
    CMissionMirror::LogEventf("rule-trip-skip-board guests=%d seats=%d shared-cars=%d", (int)guests.size(), seated, vehicles);
    return seated;
}

int CCommandPutNetworkPlayersInCar::CountParkedCopiesNear(const CVector& at, float radius)
{
    int count = 0;
    for (const auto& copy : aParkedCopies)
    {
        CVehicle* vehicle = CPools::GetVehicle(copy.vehicleHandle);
        if (vehicle && vehicle->m_fHealth > 0.0f && DistanceBetweenPoints(vehicle->GetPosition(), at) <= radius)
            ++count;
    }
    return count;
}

bool CCommandPutNetworkPlayersInCar::IsParkedCopy(CVehicle* vehicle)
{
    if (!vehicle)
        return false;
    const int handle = CPools::GetVehicleRef(vehicle);
    for (const auto& copy : aParkedCopies)
        if (copy.vehicleHandle == handle)
            return true;
    return false;
}

void CCommandPutNetworkPlayersInCar::AddParkedCopy(int vehicleHandle, CRunningScript* mirrorScript)
{
	// blue, as the game marks a vehicle to get on
	const int blip = CRadar::SetEntityBlip(BLIP_CAR, vehicleHandle, 0, BLIP_DISPLAY_BOTH);
	if (blip != -1)
	{
		CRadar::ChangeBlipColour(blip, BLIP_COLOUR_BLUE);
		CRadar::SetBlipFriendly(blip, 1);
		if (mirrorScript)
		{
			const int savedParam = ScriptParams[0];
			ScriptParams[0] = blip;
			CMirrorBlips::OnCommandExecuted(mirrorScript, 0x0186);
			ScriptParams[0] = savedParam;
		}
	}
	aParkedCopies.push_back({vehicleHandle, blip});
}

// guest-car-blip-hide: another player sits in the vehicle
static bool HasNetworkPlayerInside(CVehicle* pVehicle)
{
	if (CNetworkPlayerManager::GetPlayer(pVehicle->m_pDriver))
		return true;
	for (CPed* pPassenger : pVehicle->m_apPassengers)
	{
		if (CNetworkPlayerManager::GetPlayer(pPassenger))
			return true;
	}
	return false;
}

void CCommandPutNetworkPlayersInCar::ProcessParkedCopies()
{
	if (aParkedCopies.empty())
		return;

	CPlayerPed* pPlayerPed = FindPlayerPed(0);
	// the authority's mission is over here (ON_MISSION_FLAG_SYNC)
	const bool bMissionOver = !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag];
	for (size_t i = 0; i < aParkedCopies.size();)
	{
		const SParkedCopy copy = aParkedCopies[i];
		CVehicle* pVehicle = CPools::GetVehicle(copy.vehicleHandle);
		const bool bOnIt = pVehicle && pPlayerPed && pPlayerPed->m_nPedFlags.bInVehicle && pPlayerPed->m_pVehicle == pVehicle;
		if (pVehicle && !bOnIt && !bMissionOver && pVehicle->m_fHealth > 0.0f)
		{
			// the blip of a copy a guest got on goes while he is in it (the mirror layer carries the change)
			const int traceIndex = copy.blipHandle != -1 ? CRadar::GetActualBlipArrayIndex(copy.blipHandle) : -1;
			if (CConfigLoader::ms_bGuestCarBlipHide && traceIndex >= 0)
			{
				const eBlipDisplay display = HasNetworkPlayerInside(pVehicle) ? BLIP_DISPLAY_NEITHER : BLIP_DISPLAY_BOTH;
				if (CRadar::ms_RadarTrace[traceIndex].m_nBlipDisplay != display)
					CRadar::ChangeBlipDisplay(copy.blipHandle, display);
			}
			i++;
			continue;
		}

		if (copy.blipHandle != -1)
			CRadar::ClearBlip(copy.blipHandle);
		// a random vehicle from now on: the population removes it once nobody uses it (see the handler)
		if (pVehicle)
			Command<Commands::MARK_CAR_AS_NO_LONGER_NEEDED>(copy.vehicleHandle);
		aParkedCopies.erase(aParkedCopies.begin() + i);
	}
}
