#include "network/packets/vehicles.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CTrainSync.h>
#include "CJackSync.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <COpCodeSync.h>

static bool IsAmbientCreatedBy(int createdBy)
{
    return createdBy == RANDOM_VEHICLE || createdBy == PARKED_VEHICLE;
}

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

static bool HasAnyOccupant(CVehicle* vehicle)
{
    if (vehicle->m_pDriver)
        return true;

    for (int i = 0; i < 8; i++)
    {
        if (vehicle->m_apPassengers[i])
            return true;
    }

    return false;
}

// the seat is already held here by somebody else: warping another player into it makes two clients fight for
// the same seat (seat 0 = driver, 0..7 = passenger). The local player's seat always wins; between two network
// players nobody is warped until the server decides (see VEHICLE_SEAT_DENIED).
static bool IsSeatHeldLocally(CVehicle* vehicle, int seat, CPed* ped)
{
    CPed* occupant = seat < 0 ? vehicle->m_pDriver : (seat < 8 ? vehicle->m_apPassengers[seat] : nullptr);

    if (occupant == nullptr || occupant == ped)
        return false;

    if (occupant == FindPlayerPed(0))
        return true;

    return CNetworkPlayerManager::GetPlayer(occupant) != nullptr;
}

// a seat index is only valid for this model: the game doesn't check it before warping (it writes out of
// m_apPassengers / uses an invalid door)
static bool IsValidPassengerSeat(CVehicle* vehicle, int seat)
{
    return seat >= 0 && seat < 8 && seat < vehicle->m_nMaxPassengers;
}

// two clients created an ambient vehicle on the same spot (parked car generators, traffic): keep the one created
// by the lower player id on every client, otherwise both copies overlap and explode.
// returns true if the incoming vehicle must not be created here
static bool ResolveAmbientVehicleOverlap(Packets::Vehicles::VehicleSpawn* pVehicleSpawn)
{
    if (!IsAmbientCreatedBy(pVehicleSpawn->createdBy))
        return false;

    playerid_t remoteCreator = pVehicleSpawn->vehicleid.creator();

    for (size_t i = 0; i < CNetworkVehicleManager::m_pVehicles.size(); i++)
    {
        CNetworkVehicle* local = CNetworkVehicleManager::m_pVehicles[i];
        CVehicle* vehicle = local->m_pVehicle;

        if (!local->m_bSyncing || !vehicle || !vehicle->m_matrix || !IsAmbientCreatedBy(vehicle->m_nCreatedBy))
            continue;

        if ((vehicle->m_matrix->pos - pVehicleSpawn->pos).Magnitude() > 4.0f || HasAnyOccupant(vehicle))
            continue;

        if (CNetworkPlayerManager::m_localPlayerId.value() < remoteCreator.value())
            return true;  // ours stays, the other client removes its copy

        // theirs stays: remove ours here and on the other clients
        Packets::Vehicles::VehicleRemove packet{};
        packet.vehicleid = local->m_vehicleId;
        GetPacketFactory().Send(packet);

        local->m_bSyncing = false;  // the destructor now deletes the game vehicle without sending another remove
        CNetworkVehicleManager::Remove(local);
        delete local;
        return false;
    }

    return false;
}

PACKET_HANDLER(ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn)
{
    if (ResolveAmbientVehicleOverlap(pVehicleSpawn))
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE SPAWN %s %d %.1f %.1f %.1f %.1f", to_string(pVehicleSpawn->vehicleid).c_str(),
        pVehicleSpawn->modelid, pVehicleSpawn->pos.x, pVehicleSpawn->pos.y, pVehicleSpawn->pos.z,
        pVehicleSpawn->rot.m_angle);
#endif

    // each player runs his own copy of the mission: a remote mission vehicle over ours must not collide with it
    bool bGhost = pVehicleSpawn->createdBy == MISSION_VEHICLE && COpCodeSync::IsLocalOnlyMissionRunning() &&
                  IsLocalMissionVehicleNear(pVehicleSpawn->pos, 4.0f);

    bool createNow =
        CStreamingSync::ShouldCreateVehicleAt(pVehicleSpawn->modelid, pVehicleSpawn->createdBy, pVehicleSpawn->pos);

    CNetworkVehicle* pNetworkVehicle =
        new CNetworkVehicle(pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos,
            pVehicleSpawn->rot.m_angle, pVehicleSpawn->color1, pVehicleSpawn->color2, pVehicleSpawn->createdBy, createNow);

    // invalid model or vehicle pool full
    if (!pNetworkVehicle->m_pVehicle && !pNetworkVehicle->m_bStreamedOut)
    {
        // a train never creates its vehicle here: keep the record as a shell for CTrainSync to bind the copy
        // into (with the switch off it is dropped, as trains always were)
        if (Packets::Trains::IsTrainModel(pVehicleSpawn->modelid) && CConfigLoader::ms_bTrainSync)
        {
            pNetworkVehicle->m_bStreamedOut = true;
            pNetworkVehicle->m_bGhost = false;
            CNetworkVehicleManager::Add(pNetworkVehicle);
            return;
        }

        delete pNetworkVehicle;
        return;
    }

    pNetworkVehicle->m_bGhost = bGhost;
    CNetworkVehicleManager::Add(pNetworkVehicle);

    if (bGhost && pNetworkVehicle->m_pVehicle)
        pNetworkVehicle->m_pVehicle->m_bUsesCollision = false;
}

PACKET_HANDLER(ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE REMOVE %s", to_string(pVehicleRemove->vehicleid).c_str());
#endif
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid);
    if (pNetworkVehicle)
    {
        // a bound copy carriage: the whole copy chain goes with it
        if (CTrainSync::OnCarriageRecordRemoved(pNetworkVehicle))
            return;

        CNetworkVehicleManager::Remove(pNetworkVehicle);
        delete pNetworkVehicle;
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid);

    if (pNetworkVehicle == nullptr)
        return;

    if (pNetworkVehicle->m_bStreamedOut)
    {
        pNetworkVehicle->m_vecLastPos = pVehicleIdleUpdate->pos;
        pNetworkVehicle->m_fLastHealth = pVehicleIdleUpdate->health;
        pNetworkVehicle->m_nLastColor1 = pVehicleIdleUpdate->color1;
        pNetworkVehicle->m_nLastColor2 = pVehicleIdleUpdate->color2;
        pNetworkVehicle->m_nPaintJob = pVehicleIdleUpdate->paintjob;
        return;
    }

    if (!pNetworkVehicle->m_pVehicle)
        return;

    if (pNetworkVehicle->m_pVehicle->m_matrix == nullptr)
        return;

    pNetworkVehicle->ApplyNetworkPosition(pVehicleIdleUpdate->pos);

    pNetworkVehicle->ApplyNetworkRotation(pVehicleIdleUpdate->roll, pVehicleIdleUpdate->rot);


    pNetworkVehicle->m_pVehicle->m_vecMoveSpeed = pVehicleIdleUpdate->velocity;
    pNetworkVehicle->m_pVehicle->m_vecTurnSpeed = pVehicleIdleUpdate->turnSpeed;

    pNetworkVehicle->m_pVehicle->m_nPrimaryColor = pVehicleIdleUpdate->color1;
    pNetworkVehicle->m_pVehicle->m_nSecondaryColor = pVehicleIdleUpdate->color2;
    pNetworkVehicle->m_pVehicle->m_fHealth = pVehicleIdleUpdate->health;

    if (pNetworkVehicle->m_pVehicle->GetRemapIndex() != pVehicleIdleUpdate->paintjob)
        pNetworkVehicle->m_pVehicle->SetRemap(pVehicleIdleUpdate->paintjob);

    if (CUtil::GetVehicleType(pNetworkVehicle->m_pVehicle) == eVehicleType::VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pNetworkVehicle->m_pVehicle;

        pPlane->m_fLandingGearStatus = pVehicleIdleUpdate->planeGearState;
    }
    else if (pNetworkVehicle->m_pVehicle->m_nVehicleSubType == VEHICLE_HELI)
    {
        pNetworkVehicle->m_bRemoteRotorOn = pVehicleIdleUpdate->planeGearState > 0.0f;
        pNetworkVehicle->m_bRemoteSearchLight = pVehicleIdleUpdate->planeGearState >= 2.0f;
    }

    pNetworkVehicle->m_pVehicle->m_eDoorLock = pVehicleIdleUpdate->locked;

    // alarm / siren and lights of an empty car
    pNetworkVehicle->m_pVehicle->m_nVehicleFlags.bSirenOrAlarm = pVehicleIdleUpdate->siren;
    pNetworkVehicle->m_pVehicle->m_nVehicleFlags.bLightsOn = pVehicleIdleUpdate->lights;
    if (pVehicleIdleUpdate->horn && pNetworkVehicle->m_pVehicle->m_nHornCounter == 0)
    {
        pNetworkVehicle->m_pVehicle->PlayCarHorn();
    }
}

// the driver's game moved his vehicle far away at once (a mod shop puts it in a hidden interior, scripts warp it...):
// a local passenger would be dragged into a place his game isn't prepared to show (frozen screen, broken colours),
// so he gets off right where the vehicle was and waits there
static void LeavePassengerSeatIfVehicleWarps(CVehicle* pVehicle, const CVector& newPos)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || !localPlayer->m_nPedFlags.bInVehicle || localPlayer->m_pVehicle != pVehicle ||
        pVehicle->m_pDriver == localPlayer)
        return;

    CVector oldPos = pVehicle->GetPosition();
    if (DistanceBetweenPoints(oldPos, newPos) < 30.0f)
        return;

    CVector exitPos = oldPos + pVehicle->m_matrix->right * 3.0f;
    plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(localPlayer), exitPos.x, exitPos.y, exitPos.z);
    CHud::SetHelpMessage("Tu companero entro a un taller o fue teletransportado: esperalo aca y volve a subir cuando salga.",
        true, false, false);
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleDriverUpdate->senderPlayerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
        return;

    // the passenger hears the driver's station (only when it changes, so the passenger can still retune).
    // per vehicle: a single global value never retunes again after changing cars
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer && pLocalPlayer->m_nPedFlags.bInVehicle && pNetworkVehicle->m_pVehicle &&
        pLocalPlayer->m_pVehicle == pNetworkVehicle->m_pVehicle && pNetworkVehicle->m_pVehicle->m_pDriver != pLocalPlayer)
    {
        int8_t station = pVehicleDriverUpdate->radioStation;
        if (station >= 0 && station != pNetworkVehicle->m_lastDriverRadioStation &&
            AudioEngine.IsVehicleRadioActive() && !AudioEngine.IsRadioRetuneInProgress() &&
            AudioEngine.GetCurrentRadioStationID() != station)
        {
            AudioEngine.RetuneRadio(station);
        }
        pNetworkVehicle->m_lastDriverRadioStation = station;
    }
    else if (pLocalPlayer && !pLocalPlayer->m_nPedFlags.bInVehicle)
    {
        pNetworkVehicle->m_lastDriverRadioStation = -2;  // tune again on the next ride
    }

    // the driver's trailer (attached/detached on the driver's side)
    if (CVehicle* pTractor = pNetworkVehicle->m_pVehicle)
    {
        CNetworkVehicle* pNetworkTrailer = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->trailerid);
        CVehicle* pTrailer = pNetworkTrailer ? pNetworkTrailer->m_pVehicle : nullptr;
        if (pTractor->m_pTrailer != pTrailer)
        {
            if (pTractor->m_pTrailer)
                pTractor->m_pTrailer->BreakTowLink();
            if (pTrailer && !pTrailer->m_pTractor)
                pTrailer->SetTowLink(pTractor, true);
        }
    }

    if (pNetworkVehicle->m_bStreamedOut)
    {
        pNetworkVehicle->m_vecLastPos = pVehicleDriverUpdate->pos;
        pNetworkVehicle->StreamIn();
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid() || pVehicle->m_matrix == nullptr ||
        pNetworkPlayer->m_pPed == nullptr)
        return;

    if (CJackSync::IsPlayerJackInProgress(pNetworkPlayer->m_pPed))
    {
        pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehicleDriverUpdate->playerHealth;
        pNetworkPlayer->m_pPed->m_fHealth = pVehicleDriverUpdate->playerHealth.iHealth;
        pNetworkPlayer->m_pPed->m_fArmour = pVehicleDriverUpdate->playerHealth.iArmour;
        return;
    }

    LeavePassengerSeatIfVehicleWarps(pVehicle, pVehicleDriverUpdate->pos);

    // a victim that is being jacked must not be put back in the seat by its own driver sync
    // the copy's cosmetic NPC driver goes before a real occupant warps in (the warp would leave it loose)
    if (CTrainSync::IsSyncedCarriage(pNetworkVehicle))
        CTrainSync::EvictCosmeticDriver(pVehicle);

    if (!CJackSync::IsWarpSuppressed(pVehicleDriverUpdate->senderPlayerid.value) &&
        (pNetworkPlayer->m_pPed->m_pVehicle != pVehicle || !pNetworkPlayer->m_pPed->m_nPedFlags.bInVehicle) &&
        !IsSeatHeldLocally(pVehicle, -1, pNetworkPlayer->m_pPed))
    {
        pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
    }

    // a carriage's motion belongs to TRAIN_STATE: the seat above applies, the transform below does not
    if (CTrainSync::IsSyncedCarriage(pNetworkVehicle))
    {
        pNetworkVehicle->m_playerDriverSnapshot = *pVehicleDriverUpdate;
        return;
    }

    pNetworkVehicle->ApplyNetworkPosition(pVehicleDriverUpdate->pos);
    pNetworkVehicle->ApplyNetworkRotation(pVehicleDriverUpdate->roll, pVehicleDriverUpdate->rot);
    pVehicle->m_vecMoveSpeed = pVehicleDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pVehicleDriverUpdate->turnSpeed;

    pNetworkPlayer->NoteNetState(pVehicleDriverUpdate->playerHealth.iHealth, pVehicleDriverUpdate->playerHealth.iArmour,
        &pVehicleDriverUpdate->playerWeapon, nullptr);
    pNetworkPlayer->ApplyWeaponSnapshot(pVehicleDriverUpdate->playerWeapon);
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehicleDriverUpdate->playerHealth;
    pNetworkPlayer->m_pPed->m_fHealth = pVehicleDriverUpdate->playerHealth.iHealth;
    pNetworkPlayer->m_pPed->m_fArmour = pVehicleDriverUpdate->playerHealth.iArmour;
    pNetworkPlayer->SetKeys(pVehicleDriverUpdate->playerKeys);

    pVehicle->m_nPrimaryColor = pVehicleDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pVehicleDriverUpdate->color2;

    pVehicle->m_fHealth = pVehicleDriverUpdate->health;

    if (pVehicle->GetRemapIndex() != pVehicleDriverUpdate->paintjob)
        pVehicle->SetRemap(pVehicleDriverUpdate->paintjob);

    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
        pAutomobile->m_wMiscComponentAngle = pVehicleDriverUpdate->miscComponentAngle;
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pVehicleDriverUpdate->bikeLean;
    }

    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;

        pPlane->m_fLandingGearStatus = pVehicleDriverUpdate->planeGearState;
    }

    pNetworkVehicle->m_pVehicle->m_eDoorLock = pVehicleDriverUpdate->locked;

    pVehicle->m_nVehicleFlags.bSirenOrAlarm = pVehicleDriverUpdate->siren;
    pVehicle->m_nVehicleFlags.bLightsOn = pVehicleDriverUpdate->lights;
    // PlayCarHorn is an NPC honk with a random gate and cooldown, not a player's held horn.
    if (CConfigLoader::ms_bRemoteHornSync)
    {
        pVehicle->m_nHornCounter = pVehicleDriverUpdate->horn ? 1 : 0;
    }
    else if (pVehicleDriverUpdate->horn && pVehicle->m_nHornCounter == 0)
    {
        pVehicle->PlayCarHorn();
    }

    pNetworkVehicle->m_playerDriverSnapshot = *pVehicleDriverUpdate;
}

PACKET_HANDLER(ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleEnter->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    pNetworkVehicle->StreamIn();

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;

    if (pPlayerPed == nullptr || pVehicle == nullptr || !pPlayerPed->IsVTableValid() || !pVehicle->IsVTableValid())
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %s entered vehicleid %s %s", to_string(pVehicleEnter->senderPlayerid.value).c_str(),
        to_string(pVehicleEnter->vehicleid).c_str(), pVehicleEnter->seatid != 0 ? "as passenger" : "");
#endif

    // PLAYER_JACKED already started the paired native replay on this reliable channel.
    if (CJackSync::IsPlayerJackInProgress(pPlayerPed))
        return;

    // drop the jetpack if present
    if (auto pTask = pPlayerPed->m_pIntelligence->GetTaskJetPack())
    {
        pTask->MakeAbortable(pPlayerPed, ABORT_PRIORITY_URGENT, nullptr);
        pTask->m_nThrustStop = 0;
        pTask->m_nThrustFwd = 0;
        pTask->m_fThrustStrafe = 0.0f;
    }

    if (pVehicleEnter->bPassenger)
    {
        // a seat that this model doesn't have (or a negative index) would go straight into the game's tasks
        if (!IsValidPassengerSeat(pVehicle, pVehicleEnter->seatid))
            return;

        // another player already holds it here: wait for the server (VEHICLE_SEAT_DENIED) instead of a tug of war
        if (IsSeatHeldLocally(pVehicle, pVehicleEnter->seatid, pPlayerPed))
            return;

        pNetworkPlayer->EnterVehiclePassenger(pVehicle, pVehicleEnter->seatid);
    }
    else
    {
        // the driver seat is ours (or another player's) here: never pull a driver out for an entering player
        if (IsSeatHeldLocally(pVehicle, -1, pPlayerPed))
            return;

        if (pVehicleEnter->bForce)
        {
            pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
        }
        else
        {
            pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never
            auto pTask = new CTaskComplexEnterCarAsDriver(pVehicle);
            pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleExit->senderPlayerid);

    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    if (pPlayerPed == nullptr || pPlayerPed->m_pVehicle == nullptr || !pPlayerPed->m_nPedFlags.bInVehicle)
    {
        return;
    }

    // The victim's game can finish first; its exit must not truncate our native drag.
    if (CJackSync::IsPlayerJackInProgress(pPlayerPed))
        return;

    CVehicle* pVehicle = pPlayerPed->m_pVehicle;

    if (!pVehicle->IsVTableValid() || !pPlayerPed->IsVTableValid())
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %s exited from vehicle", to_string(pVehicleExit->senderPlayerid.value).c_str());
#endif

    if (pVehicleExit->bForce)
    {
        pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal
        CVector doorPos{};
        int door = CCarEnterExit::ComputeTargetDoorToExit(pVehicle, pPlayerPed);
        auto task = CTaskSimpleCarSetPedOut(pVehicle, door, false);
        task.ProcessPed(pPlayerPed);
        CUtil::ReleaseStackCarTask(task);
    }
    else
    {
        pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal
        auto* task = new CTaskComplexLeaveCar(pVehicle, 0, 0, true, false);
        pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
    }
}

// the server kept another player in that seat: get out here instead of fighting for it on every update
PACKET_HANDLER(ePacketType::VEHICLE_SEAT_DENIED, Packets::Vehicles::VehicleSeatDenied* pVehicleSeatDenied)
{
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer == nullptr || !pLocalPlayer->m_nPedFlags.bInVehicle || pLocalPlayer->m_pVehicle == nullptr ||
        pLocalPlayer->m_pVehicle->m_matrix == nullptr)
        return;

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleSeatDenied->vehicleid);
    CVehicle* pVehicle = pNetworkVehicle != nullptr ? pNetworkVehicle->m_pVehicle : nullptr;
    if (pVehicle == nullptr || pVehicle != pLocalPlayer->m_pVehicle)
        return;

    if (pVehicleSeatDenied->seatid < 0)
    {
        // driver seat
        if (pVehicle->m_pDriver != pLocalPlayer)
            return;
    }
    else
    {
        if (pVehicle->m_pDriver == pLocalPlayer || !IsValidPassengerSeat(pVehicle, pVehicleSeatDenied->seatid) ||
            pVehicle->m_apPassengers[pVehicleSeatDenied->seatid] != pLocalPlayer)
            return;
    }

    // An in-flight seat claim from the victim must not replace the dragged-out task with a warp.
    if (CJackSync::IsPlayerJackInProgress(pLocalPlayer))
        return;

    // leave the car right here: keeping the seat would only make every update warp both players into it again
    CVector exitPos = pVehicle->GetPosition() + pVehicle->m_matrix->right * 3.0f;
    plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(
        CPools::GetPedRef(pLocalPlayer), exitPos.x, exitPos.y, exitPos.z);
    CHud::SetHelpMessage("Ese asiento ya lo ocupa otro jugador.", true, false, false);
}

PACKET_HANDLER(ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VehicleDamage__Handle");
#endif

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        if (pNetworkVehicle->m_bStreamedOut)
        {
            pNetworkVehicle->m_oldDamageState = pVehicleDamage->damageManager;
            pNetworkVehicle->m_bHasDamageState = true;
        }

        if (auto pVehicle = pNetworkVehicle->m_pVehicle)
        {
            if (pVehicle->IsVTableValid())
            {
                if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
                {
                    CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
                    pAutomobile->m_damageManager = pVehicleDamage->damageManager;
                    pAutomobile->SetupDamageAfterLoad();
                    pNetworkVehicle->m_oldDamageState = pAutomobile->m_damageManager;
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    if (pVehicleComponentAdd->componentid >= 1000 && pVehicleComponentAdd->componentid <= 1193 &&
        std::find(pNetworkVehicle->m_aComponents.begin(), pNetworkVehicle->m_aComponents.end(),
            pVehicleComponentAdd->componentid) == pNetworkVehicle->m_aComponents.end())
    {
        pNetworkVehicle->m_aComponents.push_back(pVehicleComponentAdd->componentid);
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    // vehicle upgrade models are 1000..1193
    if (pVehicleComponentAdd->componentid < 1000 || pVehicleComponentAdd->componentid > 1193)
        return;

    CStreaming::RequestModel(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    CStreaming::RequestVehicleUpgrade(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);

    if (CStreaming::HasVehicleUpgradeLoaded(pVehicleComponentAdd->componentid))
    {
        pVehicle->AddVehicleUpgrade(pVehicleComponentAdd->componentid);
        return;
    }

    // not loaded yet: added in a later frame instead of waiting here (the game thread used to sleep up to 50 ms)
    CNetworkVehicleManager::AddPendingComponent(pVehicleComponentAdd->vehicleid, pVehicleComponentAdd->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_COMPONENT_REMOVE, Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    auto& components = pNetworkVehicle->m_aComponents;
    components.erase(std::remove(components.begin(), components.end(), pVehicleComponentRemove->componentid),
        components.end());

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    if (pVehicleComponentRemove->componentid < 1000 || pVehicleComponentRemove->componentid > 1193)
        return;

    pVehicle->RemoveVehicleUpgrade(pVehicleComponentRemove->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_PASSENGER_UPDATE, Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehiclePassengerUpdate->senderPlayerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkVehicle->StreamIn();

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;

    if (pVehicle == nullptr || pPlayerPed == nullptr)
    {
        return;
    }

    if (!pVehicle->IsVTableValid() || !pPlayerPed->IsVTableValid())
    {
        return;
    }

    if (!IsValidPassengerSeat(pVehicle, pVehiclePassengerUpdate->seatid))
        return;

    if (CJackSync::IsPlayerJackInProgress(pPlayerPed))
    {
        pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehiclePassengerUpdate->playerHealth;
        pPlayerPed->m_fHealth = pVehiclePassengerUpdate->playerHealth.iHealth;
        pPlayerPed->m_fArmour = pVehiclePassengerUpdate->playerHealth.iArmour;
        return;
    }

    if (!CJackSync::IsWarpSuppressed(pVehiclePassengerUpdate->senderPlayerid.value) &&
        (!pPlayerPed->m_nPedFlags.bInVehicle || pPlayerPed->m_pVehicle != pVehicle ||
            pNetworkVehicle->m_pVehicle->m_apPassengers[pVehiclePassengerUpdate->seatid] != pNetworkPlayer->m_pPed) &&
        !IsSeatHeldLocally(pVehicle, pVehiclePassengerUpdate->seatid, pPlayerPed))
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("forcing enter passenger %s", to_string(pNetworkPlayer->m_playerId).c_str());
#endif

        pNetworkPlayer->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pVehiclePassengerUpdate->seatid);
    }

    pNetworkPlayer->NoteNetState(pVehiclePassengerUpdate->playerHealth.iHealth, pVehiclePassengerUpdate->playerHealth.iArmour,
        &pVehiclePassengerUpdate->playerWeapon, nullptr);
    pNetworkPlayer->ApplyWeaponSnapshot(pVehiclePassengerUpdate->playerWeapon);
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehiclePassengerUpdate->playerHealth;
    pNetworkPlayer->m_pPed->m_fHealth = pVehiclePassengerUpdate->playerHealth.iHealth;
    pNetworkPlayer->m_pPed->m_fArmour = pVehiclePassengerUpdate->playerHealth.iArmour;
    pNetworkPlayer->SetKeys(pVehiclePassengerUpdate->playerKeys);

    if (pVehiclePassengerUpdate->driveby && !CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StartDriveby(pPlayerPed);
    }
    else if (!pVehiclePassengerUpdate->driveby && CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StopDriveby(pPlayerPed);
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_VEHICLE, Packets::Vehicles::AssignVehicleSyncer* pAssignVehicleSyncer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pAssignVehicleSyncer->vehicleid);

    if (!pNetworkVehicle)
    {
        return;
    }

    // explicit state: a repeated or out of order packet can't flip it back
    if (pNetworkVehicle->m_bSyncing == pAssignVehicleSyncer->syncing)
    {
        return;
    }

    // a carriage never recreates or takes over: its motion is the authority's TRAIN_STATE
    if (CTrainSync::IsSyncedCarriage(pNetworkVehicle))
    {
        pNetworkVehicle->m_bSyncing = pAssignVehicleSyncer->syncing;
        return;
    }

    if (pNetworkVehicle->m_bSyncing)
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("NOT SYNCING VEHICLE %s ANYMORE", to_string(pAssignVehicleSyncer->vehicleid).c_str());
#endif
        pNetworkVehicle->m_bSyncing = false;

        if (auto pVehicle = pNetworkVehicle->m_pVehicle)
        {
            pVehicle->SetVehicleCreatedBy(eVehicleCreatedBy::MISSION_VEHICLE);
        }
    }
    else
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("SYNCING VEHICLE %s", to_string(pAssignVehicleSyncer->vehicleid).c_str());
#endif

        // B2 handover: the previous owner let the vehicle go, create it where it left it
        bool takeover = pAssignVehicleSyncer->takeover && CConfigLoader::ms_bHandover;
        if (takeover)
        {
            pNetworkVehicle->m_vecLastPos = pAssignVehicleSyncer->pos;
            pNetworkVehicle->m_fLastHeading = pAssignVehicleSyncer->heading;
        }

        pNetworkVehicle->StreamIn();

        // the game pool is full or the vehicle is out of streaming range: give it back, another player may take it
        if (takeover && pNetworkVehicle->m_pVehicle == nullptr)
        {
            Packets::Vehicles::VehicleRelease packet{};
            packet.vehicleid = pNetworkVehicle->m_vehicleId;
            packet.pos = pAssignVehicleSyncer->pos;
            packet.areaCode = 0;  // the current interior is unknown for a vehicle record
            packet.reason = 1;
            GetPacketFactory().Send(packet);

            pNetworkVehicle->m_bSyncing = false;
            return;
        }

        pNetworkVehicle->m_bSyncing = true;

        if (auto pVehicle = pNetworkVehicle->m_pVehicle)
        {
            pVehicle->SetVehicleCreatedBy(pNetworkVehicle->m_nCreatedBy);

            if (takeover)
            {
                // keep-alive so the game doesn't cull it right away (same trick as CreateHosted)
                pVehicle->m_nTimeTillWeNeedThisCar = CTimer::m_snTimeInMilliseconds + 10000;

                // an NPC driver of an ambient car must drive it around like the traffic would
                if (pVehicle->m_pDriver != nullptr && CStreamingSync::IsAmbientVehicle(pNetworkVehicle->m_nCreatedBy))
                {
                    plugin::Command<Commands::TASK_CAR_DRIVE_WANDER>(CPools::GetPedRef(pVehicle->m_pDriver),
                        CPools::GetVehicleRef(pVehicle), 10.0f, 0 /* DRIVINGMODE_STOPFORCARS */);
                }
            }
        }
    }
}


PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy)
{
    if (CLocalPlayer::m_bIsHost)
    {
        return;
    }

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
    {
        pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;

        if (pNetworkVehicle->m_bSyncing)
        {
            if (auto pVehicle = pNetworkVehicle->m_pVehicle)
            {
                pVehicle->SetVehicleCreatedBy(pSetVehicleCreatedBy->createdBy);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DOORS, Packets::Vehicles::VehicleDoors* pVehicleDoors)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDoors->vehicleid);
    if (pNetworkVehicle == nullptr || pNetworkVehicle->m_bSyncing)
        return;

    // keep the last state: when a ped is entering / leaving the car on this client it is applied later (UpdateIdle)
    memcpy(pNetworkVehicle->m_anRecvDoorRatios, pVehicleDoors->ratio, sizeof(pNetworkVehicle->m_anRecvDoorRatios));
    pNetworkVehicle->m_bDoorsPending = !CNetworkVehicleManager::ApplyRemoteDoors(pNetworkVehicle);
}

PACKET_HANDLER(ePacketType::VEHICLE_HYDRAULIC_SYNC, Packets::Vehicles::VehicleHydraulicSync* pVehicleHydraulicSync)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleHydraulicSync->vehicleid);
    if (pNetworkVehicle == nullptr)
        return;

    // keep the last input: it is replayed on every ProcessControl of the remote vehicle
    pNetworkVehicle->m_hydraulicAngle = pVehicleHydraulicSync->angle;
    pNetworkVehicle->m_hydraulicGunLeftRight = pVehicleHydraulicSync->gunLeftRight;
    pNetworkVehicle->m_hydraulicGunUpDown = pVehicleHydraulicSync->gunUpDown;
    pNetworkVehicle->m_bHydraulicJump = pVehicleHydraulicSync->jump;
    pNetworkVehicle->m_bHasHydraulicState = true;

    if (pNetworkVehicle->m_pVehicle != nullptr && pNetworkVehicle->m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE &&
        pNetworkVehicle->m_pVehicle->m_nHandlingFlags.bHydraulicInst)
    {
        ((CAutomobile*)pNetworkVehicle->m_pVehicle)->m_wMiscComponentAngle = pVehicleHydraulicSync->angle;
    }
}

PACKET_HANDLER(ePacketType::PLAYER_JACKED, Packets::Vehicles::PlayerJacked* pPlayerJacked)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPlayerJacked->vehicleid);
    if (pNetworkVehicle == nullptr || pNetworkVehicle->m_pVehicle == nullptr)
        return;

    if (CJackSync::ReplayThief(pPlayerJacked->senderPlayerid.value, pPlayerJacked->victimPlayerId,
            pPlayerJacked->vehicleid, pPlayerJacked->seatid, pPlayerJacked->door))
        return;

    // the local player is the victim: leave the seat here, its own sync takes it out on the other clients
    if (pPlayerJacked->victimPlayerId == CNetworkPlayerManager::m_localPlayerId)
    {
        // stop our own driver sync for a moment: the seat is the jacker's now, ours would take it back
        CJackSync::SuppressWarp(CNetworkPlayerManager::m_localPlayerId);
        CJackSync::ApplyLocalJacked(pPlayerJacked->vehicleid, pPlayerJacked->seatid, pPlayerJacked->door);
        return;
    }

    CNetworkPlayer* pVictim = CNetworkPlayerManager::GetPlayer(pPlayerJacked->victimPlayerId);
    if (pVictim == nullptr)
        return;

    CJackSync::SuppressWarp(pPlayerJacked->victimPlayerId);

    // the jacker already has the drag locally: it only needed the warp suppression
    if (pPlayerJacked->senderPlayerid.value == CNetworkPlayerManager::m_localPlayerId)
        return;

    CJackSync::PlayRemoteJacked(
        pVictim->m_pPed, pNetworkVehicle->m_pVehicle, pPlayerJacked->seatid, pPlayerJacked->door);
}

PACKET_HANDLER(ePacketType::PLAYER_BEING_JACKED, Packets::Vehicles::PlayerBeingJacked* pPlayerBeingJacked)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPlayerBeingJacked->vehicleid);
    if (pNetworkVehicle == nullptr || pNetworkVehicle->m_pVehicle == nullptr)
        return;

    CNetworkPlayer* pVictim = CNetworkPlayerManager::GetPlayer(pPlayerBeingJacked->senderPlayerid);
    if (pVictim == nullptr)
        return;

    CJackSync::SuppressWarp(pPlayerBeingJacked->senderPlayerid.value);
    CJackSync::PlayRemoteJacked(
        pVictim->m_pPed, pNetworkVehicle->m_pVehicle, pPlayerBeingJacked->seatid, pPlayerBeingJacked->door);
}
