#include <CPedGroups.h>
#include <CCarEnterExit.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CTaskComplexLeaveCar.h>
#include "stdafx.h"
#include "CInteriorObjectSync.h"


#include "CPedDamageSync.h"
#include "CInteriorOwnership.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include "CMissionMirror.h"

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;

CNetworkPed* CNetworkPedManager::GetPed(pedid_t pedid)
{
    if (!pedid.valid())
        return nullptr;

    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_pedId == pedid)
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

CNetworkPed* CNetworkPedManager::GetPed(CEntity* entity)
{
    if (entity == nullptr)
        return nullptr;

    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_pPed == entity)
        {
            return m_pPeds[i];
        }
    }

    return nullptr;
}

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    CNetworkPedManager::m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

// called every 50 ms: peds far from every other player are sent every 2nd/4th call
static bool ShouldSendPedThisUpdate(CPed* pPed, uint32_t updateCount, size_t index)
{
    if (!CConfigLoader::ms_bNpcDistanceRate)
        return true;

    const CVector& pos = pPed->GetPosition();
    float nearest = FLT_MAX;
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_pPed)
            nearest = std::min(nearest, (pNetworkPlayer->m_pPed->GetPosition() - pos).Magnitude());
    }

    uint32_t divisor = nearest > 150.0f ? 4 : (nearest > 60.0f ? 2 : 1);
    // spread the far ones over the calls
    return (updateCount + index) % divisor == 0;
}

void CNetworkPedManager::Update()
{
    CNetworkPedManager::RemoveHostedUnused();

    static uint32_t updateCount = 0;
    updateCount++;
    size_t index = 0;

    for (CNetworkPed* pNetworkPed : m_pPeds)
    {
        index++;
        if (!pNetworkPed->m_bSyncing)
            continue;

        CPed* pPed = pNetworkPed->m_pPed;
        if (!pPed)
            continue;

        // task / animation state is not affected by the distance rate (it changes on its own events)
        pNetworkPed->UpdateTaskSync();

        if (!ShouldSendPedThisUpdate(pPed, updateCount, index))
            continue;

        CVehicle* pVehicle = pPed->m_pVehicle;
        CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;

        if (pNetworkVehicle && pPed->m_nPedFlags.bInVehicle)
        {
            bool isDriver = (pVehicle->m_pDriver == pPed);

            if (isDriver)
            {
                Packets::Peds::PedDriverUpdate packet{};

                packet.vehicleSubType = static_cast<eVehicleType>(pVehicle->m_nVehicleSubType);

                packet.pedid = pNetworkPed->m_pedId;
                packet.vehicleid = pNetworkVehicle->m_vehicleId;

                packet.pos = pVehicle->m_matrix->pos;
                packet.roll = pVehicle->m_matrix->right;
                packet.rot = pVehicle->m_matrix->up;
                packet.velocity = pVehicle->m_vecMoveSpeed;
                packet.turnSpeed = pVehicle->m_vecTurnSpeed;

                packet.pedHealth.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.pedHealth.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.pedWeapon.iWeaponType = weapon.m_eWeaponType;
                packet.pedWeapon.iWeaponState = weapon.m_nState;
                packet.pedWeapon.nAmmo = weapon.m_nAmmoInClip;

                packet.color1 = pVehicle->m_nPrimaryColor;
                packet.color2 = pVehicle->m_nSecondaryColor;

                packet.health = pVehicle->m_fHealth;

                packet.paintjob = pVehicle->GetRemapIndex();

                if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
                {
                    CBike* pBike = (CBike*)pVehicle;
                    packet.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
                }
                if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
                {
                    CPlane* pPlane = (CPlane*)pVehicle;
                    packet.planeGearState = pPlane->m_fLandingGearStatus;
                }
                else if (pVehicle->m_nVehicleSubType == VEHICLE_HELI)
                    packet.planeGearState = CNetworkVehicle::HeliWireState(pVehicle);
                if (pVehicle->m_nVehicleSubType == eVehicleType::VEHICLE_BMX)
                {
                    CBmx* pBmx = (CBmx*)pVehicle;
                    packet.controlPedaling = pBmx->m_fControlPedaling;
                }
                if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
                {
                    CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
                    packet.miscComponentAngle = pAutomobile->m_wMiscComponentAngle;
                }
                packet.locked = pVehicle->m_eDoorLock;
                packet.gasPedal = pVehicle->m_fGasPedal;
                packet.breakPedal = pVehicle->m_fBreakPedal;
                packet.steerAngle = pVehicle->m_fSteerAngle;

                packet.horn = pVehicle->m_nHornCounter != 0;
                packet.siren = pVehicle->m_nVehicleFlags.bSirenOrAlarm;

                GetPacketFactory().Send(packet);
            }
            else
            {
                Packets::Peds::PedPassengerSync packet{};
                packet.pedid = pNetworkPed->m_pedId;
                packet.vehicleid = pNetworkVehicle->m_vehicleId;

                packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
                packet.weaponSnapshot.iWeaponState = weapon.m_nState;
                packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

                // not in the passenger array for an instant (seat shuffle): the default 0 would claim the front seat
                packet.seatid = -1;
                for (int i = 0; i < pNetworkVehicle->m_pVehicle->m_nMaxPassengers && i < 8; i++)
                {
                    if (pNetworkVehicle->m_pVehicle->m_apPassengers[i] == pNetworkPed->m_pPed)
                    {
                        packet.seatid = i;
                        break;
                    }
                }
                if (packet.seatid >= 0)
                    GetPacketFactory().Send(packet);
            }
        }
        else if (!std::isfinite(pPed->m_matrix->pos.x) || !std::isfinite(pPed->m_matrix->pos.y) ||
            !std::isfinite(pPed->m_matrix->pos.z))
        {
            // our own ped got a NaN position (2026-11-12): put it back where it was last seen, never send it
            if (std::isfinite(pNetworkPed->m_vecLastPos.x) && std::isfinite(pNetworkPed->m_vecLastPos.y) &&
                std::isfinite(pNetworkPed->m_vecLastPos.z) && pNetworkPed->m_vecLastPos.Magnitude() > 1.0f)
                pPed->SetPosn(pNetworkPed->m_vecLastPos);
            pPed->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        }
        else
        {
            Packets::Peds::PedOnFoot packet{};

            packet.pedid = pNetworkPed->m_pedId;
            packet.pos = pPed->m_matrix->pos;
            packet.velocity = pPed->m_vecMoveSpeed;
            packet.hasVelocity = CConfigLoader::ms_bNpcVelocitySync;

            packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
            packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

            CWeapon& weapon = pPed->GetWeapon();
            packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
            packet.weaponSnapshot.iWeaponState = weapon.m_nState;
            packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

            packet.aimingRotation = pPed->m_fAimingRotation;
            packet.currentRotation = pPed->m_fCurrentRotation;
            packet.lookDirection = pPed->m_fLookDirection;
            packet.moveState = (eMoveState)pPed->m_nMoveState;

            packet.bDucked = CUtil::IsDucked(pPed);

            CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();

            if (useGun)
            {
                packet.bAiming = true;
                packet.weaponAim = useGun->m_pTarget && (useGun->m_vecTarget.x == 0.f || useGun->m_vecTarget.y == 0.f)
                                       ? useGun->m_pTarget->GetPosition()
                                       : useGun->m_vecTarget;
            }

            packet.fightingStyle = pPed->m_nFightingStyle;

            GetPacketFactory().Send(packet);

        }

        // On-foot sets and motorcycle-cop drive-by overlays use the same packet.
        if (CConfigLoader::ms_bNpcAnimMirror && (updateCount + index) % 2 == 0)
            pNetworkPed->SendAnimSet();
    }
}

// the enter / leave car task is still moving the copy (npc-task-sync)
static bool IsEnterLeaveCarTaskRunning(CPed* ped)
{
    if (ped == nullptr || ped->m_pIntelligence == nullptr)
        return false;

    CTaskManager& taskManager = ped->m_pIntelligence->m_TaskMgr;
    return taskManager.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_DRIVER) ||
           taskManager.FindActiveTaskByType(TASK_COMPLEX_ENTER_CAR_AS_PASSENGER) ||
           taskManager.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR);
}

namespace
{
// recruit-all-players: the main script of a guest may leave "group stuff disabled" (no recruiting, no group
// commands) switched on; outside of missions every player can recruit like the host
void EnableLocalRecruiting()
{
    // the host's own main.scm decides when his recruiting opens; a guest's lags behind (his own progress)
    if (!CConfigLoader::ms_bRecruitAllPlayers || !CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost)
        return;
    auto* player = FindPlayerPed(0);
    if (!player || !player->m_pPlayerData || player->m_fHealth <= 0.0f || CCutsceneMgr::ms_running)
        return;
    if (CTheScripts::OnAMissionFlag && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
        return;
    if (!player->m_pPlayerData->m_bGroupStuffDisabled)
        return;
    player->m_pPlayerData->m_bGroupStuffDisabled = false;
    static uint32_t lastLog = 0;
    if (GetTickCount() - lastLog > 10000)
    {
        lastLog = GetTickCount();
        CMissionMirror::LogEventf("recruit-all-enabled");
    }
}

void SyncLocalRecruits()
{
    static std::vector<pedid_t> previous;
    static uint32_t lastReport = 0;
    if (!CNetwork::m_bConnected || !CNetwork::m_bAuthenticated)
    {
        previous.clear();
        lastReport = 0;
        return;
    }
    const uint32_t now = GetTickCount();
    if (now - lastReport < 750)
        return;
    lastReport = now;

    std::vector<pedid_t> current;
    auto* player = FindPlayerPed(0);
    auto* group = player && CConfigLoader::ms_bRecruitDismissSync ? CPedGroups::GetPedsGroup(player) : nullptr;
    if (group && group->m_groupMembership.IsLeader(player) && player->m_fHealth > 0.0f)
    {
        for (int slot = 0; slot < 7; ++slot)
        {
            auto* ped = group->m_groupMembership.GetMember(slot);
            if (!ped || ped->m_fHealth <= 0.0f)
                continue;
            auto* networkPed = CNetworkPedManager::GetPed(ped);
            if (!networkPed || networkPed->m_nCreatedBy != RANDOM_CHAR || ped->m_nPedType != PED_TYPE_GANG2)
                continue;
            current.push_back(networkPed->m_pedId);
        }
    }
    for (const auto id : previous)
        if (std::find(current.begin(), current.end(), id) == current.end())
        {
            Packets::Peds::PedRecruit packet{};
            packet.pedid = id;
            GetPacketFactory().Send(packet);
        }
    for (const auto id : current)
    {
        Packets::Peds::PedRecruit packet{};
        packet.pedid = id;
        packet.recruited = true;
        GetPacketFactory().Send(packet);
    }
    previous.swap(current);
}

// recruit-board-passenger: the game only boards a group when its leader drives (EVENT_LEADER_ENTERED_CAR_AS_DRIVER,
// gta-reversed TaskComplexEnterCar.cpp). A player riding as passenger (the host or a guest, each one his own recruits)
// left them on foot (the user, 15-11): they take the free seats like CPedGroupDefaultTaskAllocatorSitInLeaderCar does
// (CTaskComplexEnterCarAsPassenger + ComputeTargetDoorToEnterAsPassenger) and get out with him.
void BoardRecruitsAsPassenger()
{
    static int carHandle = -1;
    static std::vector<std::pair<int, int>> boarded;  // ped handle and seat sent to, in that car
    static uint32_t lastCheck = 0;
    const uint32_t now = GetTickCount();
    if (now - lastCheck < 250)
        return;
    lastCheck = now;

    auto* player = FindPlayerPed(0);
    auto* group = player && CConfigLoader::ms_bRecruitBoardPassenger ? CPedGroups::GetPedsGroup(player) : nullptr;
    const bool leader = group && group->m_groupMembership.IsLeader(player) && player->m_fHealth > 0.0f;
    CVehicle* car = player && player->m_nPedFlags.bInVehicle ? player->m_pVehicle : nullptr;
    const bool passenger = leader && car && car->m_pDriver != player;
    CVehicle* previous = carHandle >= 0 ? CPools::GetVehicle(carHandle) : nullptr;

    // he got out of (or now drives) the car his recruits rode in as passengers: they get out with him
    if (previous && (!passenger || car != previous))
    {
        if (leader && (!car || car != previous))
        {
            for (int slot = 0; slot < 7; ++slot)
            {
                auto* ped = group->m_groupMembership.GetMember(slot);
                if (!ped || ped == player || ped->m_fHealth <= 0.0f || !ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != previous)
                    continue;
                const int handle = CPools::GetPedRef(ped);
                if (std::none_of(boarded.begin(), boarded.end(), [handle](const auto& b) { return b.first == handle; }))
                    continue;
                ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexLeaveCar(previous, 0, 0, true, false), TASK_PRIMARY_PRIMARY, false);
            }
            CMissionMirror::LogEventf("recruit-passenger-exit");
        }
        carHandle = -1;
        boarded.clear();
        previous = nullptr;
    }
    if (!passenger)
    {
        if (!previous)
        {
            carHandle = -1;
            boarded.clear();
        }
        return;
    }
    if (carHandle != CPools::GetVehicleRef(car))
    {
        carHandle = CPools::GetVehicleRef(car);
        boarded.clear();
    }

    // seats promised to recruits still on their way stay promised
    bool reserved[8]{};
    for (const auto& b : boarded)
        if (b.second >= 0 && b.second < 8)
            reserved[b.second] = true;
    for (int slot = 0; slot < 7; ++slot)
    {
        auto* ped = group->m_groupMembership.GetMember(slot);
        if (!ped || ped == player || ped->m_fHealth <= 0.0f || ped->m_nPedFlags.bInVehicle)
            continue;
        // only the recruits this game runs (a copy follows its owner's tasks)
        auto* networkPed = CNetworkPedManager::GetPed(ped);
        if (networkPed && !networkPed->m_bSyncing)
            continue;
        const int handle = CPools::GetPedRef(ped);
        if (std::any_of(boarded.begin(), boarded.end(), [handle](const auto& b) { return b.first == handle; }) ||
            DistanceBetweenPoints(ped->GetPosition(), car->GetPosition()) > 30.0f)
            continue;
        int seat = -1;
        for (int i = 0; i < car->m_nMaxPassengers && i < 8; ++i)
            if (car->m_apPassengers[i] == nullptr && !reserved[i])
            {
                seat = i;
                break;
            }
        if (seat < 0)
            break;
        const int door = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(car, seat);
        if (door < 0)
            continue;
        reserved[seat] = true;
        boarded.emplace_back(handle, seat);
        ped->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexEnterCarAsPassenger(car, door, false), TASK_PRIMARY_PRIMARY, false);
        CMissionMirror::LogEventf("recruit-passenger-board seat=%d", seat);
    }
}
}

void CNetworkPedManager::Process()
{
    EnableLocalRecruiting();
    SyncLocalRecruits();
    BoardRecruitsAsPassenger();
    CInteriorOwnership::MaintainHiddenPeds();
    for (auto networkPed : m_pPeds)
    {
        networkPed->ValidateCopyModel();

        // D npc-named-anim-sync: a state whose animation block is still loading is retried here
        if (networkPed->m_bPendingTaskState)
            networkPed->RetryPendingTaskState();

        if (networkPed->m_bSyncing)
            continue;

        // ped-damage-authority: a copy that stayed dead while its syncer keeps it alive is rebuilt here
        CPedDamageSync::ProcessCopy(networkPed);

        CPed* ped = networkPed->m_pPed;

        if (ped == nullptr)
            continue;

        // the enter / leave car task moves the copy itself: skip the position until it is done (or it got in)
        bool bTaskSync = networkPed->IsPosSyncSkipped();
        if (bTaskSync && (!IsEnterLeaveCarTaskRunning(ped) ||
                          (networkPed->m_bTaskSyncExpectInside && ped->m_nPedFlags.bInVehicle != 0)))
        {
            networkPed->m_nSkipPosSyncUntil = 0;
            bTaskSync = false;
        }

        // the enter / leave car tasks play their own animations
        if (!bTaskSync)
            networkPed->EnsureBaseAnimation();
        networkPed->DiagnoseInteriorCopy();

        // npc-aim-sync: the gun task of a copy needs its command every frame
        if (CConfigLoader::ms_bNpcAimSync && !bTaskSync)
            networkPed->ProcessAim();
        // npc-driveby-sync does not depend on npc-aim-sync
        if (!bTaskSync && !networkPed->m_bSyncing && ped->m_nPedFlags.bInVehicle && ped->m_pIntelligence)
            networkPed->ProcessDriveBy();

        if (networkPed->m_bHasPosError)
        {
            if (CConfigLoader::ms_bSmoothOnFoot && !ped->m_nPedFlags.bInVehicle && !bTaskSync)
                ped->SetPosn(ped->GetPosition() + NetworkSmoothing::Step(networkPed->m_vecPosError, networkPed->m_bHasPosError));
            else
                networkPed->m_bHasPosError = false;
        }

        if (!ped->m_nPedFlags.bInVehicle && !bTaskSync)
        {
            ped->m_fAimingRotation = networkPed->m_fAimingRotation;
            ped->m_fCurrentRotation = networkPed->m_fCurrentRotation;
            ped->m_fLookDirection = networkPed->m_fLookDirection;
        }
    }

    CInteriorObjectSync::FrameUpdate();
}

void CNetworkPedManager::AssignHost()
{
    /*for (auto networkPed : m_pPeds)
    {
        CPed* ped = networkPed->m_pPed;

        if (ped)
        {
            ped->SetCharCreatedBy(networkPed->m_nCreatedBy);

            CTaskComplexWander* task = plugin::CallAndReturn<CTaskComplexWander*, 0x673D00>(ped); //
    GetWanderTaskByPedType ped->m_pIntelligence->m_TaskMgr.SetTask(task, 0, false);
        }
    }*/
}

pedid_t CNetworkPedManager::AllocatePedId()
{
    COOP_VERIFY(CNetworkPlayerManager::m_localPlayerId.valid());
    return pedid_t(CNetworkPlayerManager::m_localPlayerId, ms_nNextPedId++);
}

void CNetworkPedManager::ResetIdAllocator()
{
    ms_nNextPedId = 1;
}

void CNetworkPedManager::RemoveHostedUnused()
{
    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        if ((*it)->m_bSyncing)
        {
            CPed* ped = (*it)->m_pPed;
            if (!IsPedPointerValid(ped))
            {
                delete *it;
                it = m_pPeds.erase(it);
                continue;
            }
        }
        ++it;
    }
}
