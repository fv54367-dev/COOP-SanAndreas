#include "network/packets/peds.h"
#include "network/packets/train.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CPedDamageSync.h"
#include "CJackSync.h"
#include <CTrainSync.h>
#include "CPatientHandover.h"
#include "CInteriorOwnership.h"
#include "CInteriorObjectSync.h"

static bool IsFiniteVector(const CVector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

PACKET_HANDLER(ePacketType::INTERIOR_POPULATION, Packets::Peds::InteriorPopulation* packet)
{
    CInteriorOwnership::HandlePopulation(*packet);
}

PACKET_HANDLER(ePacketType::INTERIOR_HELD_OBJECT, Packets::Peds::InteriorHeldObject* packet)
{
    CInteriorObjectSync::HandleHeldObject(packet);
}

PACKET_HANDLER(ePacketType::INTERIOR_SERVICE_POSE, Packets::Peds::InteriorServicePose* packet)
{
    CInteriorObjectSync::HandleServicePose(packet);
}
#include <CCarEnterExit.h>
#include <CPedGroups.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CBulletTraces.h>

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED SPAWN %s %d %.1f %.1f %.1f %d %d", to_string(pPedSpawn->pedid).c_str(), pPedSpawn->modelId,
        pPedSpawn->pos.x, pPedSpawn->pos.y, pPedSpawn->pos.z, pPedSpawn->pedType, pPedSpawn->createdBy);
#endif

    CNetworkPlayer* pCreator = CNetworkPlayerManager::GetPlayer(pPedSpawn->pedid.creator());
    unsigned char creatorArea = pCreator && pCreator->m_pPed ? pCreator->m_pPed->m_nAreaCode : 0;

    // the sender is in our interior: only its owner hosts the population
    bool suppress =
        CStreamingSync::ShouldSuppressPedSpawn(pPedSpawn->createdBy, pPedSpawn->pos, creatorArea);

    if (CConfigLoader::ms_bInteriorPopulationSync)
        suppress = false; // the server filters publications using current ownership, not creator id

    bool createNow =
        !suppress && CStreamingSync::ShouldCreatePedAt(pPedSpawn->createdBy, pPedSpawn->pos);

    if (auto pOldNetworkPed = CNetworkPedManager::GetPed(pPedSpawn->pedid))
    {
        // duplicated id: drop the old one locally without telling the server to remove the new one
        pOldNetworkPed->m_bSyncing = false;
        pOldNetworkPed->LogCopyReason("spawn-replaced");
        CNetworkPedManager::Remove(pOldNetworkPed);
        delete pOldNetworkPed;
    }

    CNetworkPed* pNetworkPed = new CNetworkPed(pPedSpawn->pedid, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos,
        pPedSpawn->createdBy, pPedSpawn->specialModelName, createNow);

    pNetworkPed->m_bInteriorPopulation = pPedSpawn->interiorPopulation;
    if (CConfigLoader::ms_bInteriorPopulationSync && pPedSpawn->areaCode)
    {
        creatorArea = pPedSpawn->areaCode;
        pNetworkPed->m_nAreaCode = creatorArea;
        if (pNetworkPed->m_pPed) pNetworkPed->m_pPed->m_nAreaCode = creatorArea;
    }

    if (!pNetworkPed->m_pPed && !pNetworkPed->m_bStreamedOut)
    {
        delete pNetworkPed;
        return;
    }

    if (!createNow) pNetworkPed->LogCopyReason(suppress ? "spawn-interior-suppressed" : "spawn-distance");

    if (suppress)
        pNetworkPed->SuppressDuplicate(CStreamingSync::GetLocalInteriorArea());
    else if (creatorArea != 0)
        pNetworkPed->m_nAreaCode = creatorArea;

    CNetworkPedManager::Add(pNetworkPed);
}

// paramedic coop: a command of the owner's AMBULAN on a patient we simulate (CPatientHandover)
PACKET_HANDLER(ePacketType::PED_PATIENT_TASK, Packets::Peds::PedPatientTask* pPatientTask)
{
    CPatientHandover::HandlePacket(*pPatientTask);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED REMOVE %s", to_string(pPedRemove->pedid).c_str());
#endif

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);
    if (pNetworkPed)
    {
        pNetworkPed->LogCopyReason("server-remove");
        CNetworkPedManager::Remove(pNetworkPed);
        delete pNetworkPed;
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_PED, Packets::Peds::AssignPedSyncer* pAssignPedSyncer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pAssignPedSyncer->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    // explicit state: a repeated or out of order packet can't flip it back
    if (pNetworkPed->m_bSyncing == pAssignPedSyncer->syncing)
    {
        return;
    }

    if (pNetworkPed->m_bSyncing)
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("NOT SYNCING PED %s ANYMORE", to_string(pAssignPedSyncer->pedid).c_str());
#endif
        pNetworkPed->m_bSyncing = false;

        // we are a copy now: accept the task state of the new syncer and forget ours
        pNetworkPed->m_bHasTaskSerial = false;
        pNetworkPed->m_nSkipPosSyncUntil = 0;
        pNetworkPed->m_pSyncedAnim = nullptr;
        pNetworkPed->StopHostedAnim();

        if (auto pPed = pNetworkPed->m_pPed)
        {
            pPed->SetCharCreatedBy(MISSION_CHAR);
        }
    }
    else
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("SYNCING PED %s", to_string(pAssignPedSyncer->pedid).c_str());
#endif

        // B2 handover: the previous owner let the ped go, create it where it left it
        auto* player = FindPlayerPed(0);
        auto* group = player && CConfigLoader::ms_bRecruitDismissSync ? CPedGroups::GetPedsGroup(player) : nullptr;
        const bool recruited = group && group->m_groupMembership.IsLeader(player) &&
            pNetworkPed->m_pPed && pNetworkPed->m_nCreatedBy == RANDOM_CHAR &&
            pNetworkPed->m_pPed->m_nPedType == PED_TYPE_GANG2 &&
            group->m_groupMembership.IsFollower(pNetworkPed->m_pPed);
        bool takeover = pAssignPedSyncer->takeover && (recruited || CConfigLoader::ms_bHandover ||
            (CConfigLoader::ms_bInteriorPopulationSync && pNetworkPed->m_bInteriorPopulation));
        if (takeover && IsFiniteVector(pAssignPedSyncer->pos))
        {
            pNetworkPed->m_vecLastPos = pAssignPedSyncer->pos;
            pNetworkPed->m_fCurrentRotation = pAssignPedSyncer->heading;
        }

        pNetworkPed->m_bSuppressedDuplicate = false;
        const bool adoptedService = takeover && CInteriorOwnership::AdoptServicePed(pNetworkPed);
        pNetworkPed->StreamIn();

        // the game pool is full or the ped is out of streaming range: give it back, another player may take it
        if (takeover && pNetworkPed->m_pPed == nullptr)
        {
            Packets::Peds::PedRelease packet{};
            packet.pedid = pNetworkPed->m_pedId;
            packet.pos = pAssignPedSyncer->pos;
            packet.heading = pAssignPedSyncer->heading;
            packet.health = pNetworkPed->m_fHealth;
            packet.areaCode = pNetworkPed->m_nAreaCode;
            packet.reason = 1;
            GetPacketFactory().Send(packet);

            pNetworkPed->m_bSyncing = false;
            return;
        }

        pNetworkPed->m_bSyncing = true;
        pNetworkPed->m_bClaimOnRelease = false;
        pNetworkPed->m_bHasSentTaskState = false;  // send the current task right away

        if (auto pPed = pNetworkPed->m_pPed)
        {
            pPed->SetCharCreatedBy(pNetworkPed->m_nCreatedBy);

            if (takeover && !adoptedService)
            {
                // create it facing where its previous owner left it
                pPed->SetOrientation(0.0f, 0.0f, pAssignPedSyncer->heading);

                // undo the AI that CreateGamePed neutralizes for the copies: now it is ours
                if (!(CConfigLoader::ms_bInteriorPopulationSync && pNetworkPed->m_nAreaCode != 0 &&
                    pNetworkPed->m_nAreaCode == CInteriorOwnership::GetLocalArea()))
                    pPed->SetPedDefaultDecisionMaker();

                // keep-alive so the game doesn't cull it right away (same trick as CreateHosted)
                pPed->field_54C = CTimer::m_snTimeInMilliseconds + 10000;

                // D interior handover: the peds of an interior are handed over when its owner leaves; there is no
                // AI for them here (its brains ran only on the owner and wander would walk through the walls).
                // Only a ped of our own interior takes this branch: an ambient ped of the main map (area 0) must
                // wander, even when we happen to be inside an interior.
                bool interior = CConfigLoader::ms_bInteriorHandover && pNetworkPed->m_nAreaCode != 0 &&
                                pNetworkPed->m_nAreaCode == CInteriorOwnership::GetLocalArea();

                if (recruited)
                {
                    if (group->m_bMembersEnterLeadersVehicle)
                        player->TellGroupToStartFollowingPlayer(true, false, true);
                }
                else if (interior)
                {
                    CInteriorOwnership::OnPedTakeover(pNetworkPed->m_nAreaCode);

                    // keep the last synchronised animation (dancing, sitting at the bar...) in loop; when there was
                    // none (or it was a one shot), the ped just stands still
                    if (CConfigLoader::ms_bNpcNamedAnimSync && pNetworkPed->m_bHasLastTaskState &&
                        pNetworkPed->m_lastTaskState.namedAnim &&
                        ((pNetworkPed->m_lastTaskState.animFlags & ANIMATION_LOOPED) ||
                         (CConfigLoader::ms_bInteriorPopulationSync && !(pNetworkPed->m_lastTaskState.animFlags & 1))))
                    {
                        if (!pNetworkPed->PlayHostedNamedAnim(pNetworkPed->m_lastTaskState))
                        {
                            // the block is still loading: CNetworkPedManager::Process retries this
                            pNetworkPed->m_bPendingTaskState = true;
                            pNetworkPed->m_nPendingTaskStateUntil =
                                CConfigLoader::GetSyncTime() + CNetworkPed::PENDING_TASK_STATE_TIMEOUT;
                        }
                    }
                    else if (!CConfigLoader::ms_bInteriorPopulationSync)
                    {
                        plugin::Command<Commands::TASK_STAND_STILL>(CPools::GetPedRef(pPed), -1);
                    } // keep the copy's grouped pose and neutral AI on a population takeover
                }
                // an ambient ped on foot must wander like the population would
                else if (!pPed->m_nPedFlags.bInVehicle && CStreamingSync::IsAmbientPed(pNetworkPed->m_nCreatedBy))
                {
                    plugin::Command<Commands::TASK_WANDER_STANDARD>(CPools::GetPedRef(pPed));
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);

    // a NaN position (2026-11-12, prueba_auto) would be kept and handed over with the ped: drop the packet
    if (!pNetworkPed || !IsFiniteVector(pPedOnFoot->pos) || (pPedOnFoot->hasVelocity && !IsFiniteVector(pPedOnFoot->velocity)))
    {
        return;
    }

    pNetworkPed->m_vecNetPos = pPedOnFoot->pos;
    pNetworkPed->m_nNetPosTick = GetTickCount();

    if (pNetworkPed->m_bStreamedOut)
    {
        pNetworkPed->m_vecLastPos = pPedOnFoot->pos;
        pNetworkPed->m_fHealth = pPedOnFoot->healthSnapshot.iHealth;
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (!pPed)
    {
        return;
    }

    // the enter / leave car task is moving this copy itself (npc-task-sync): don't teleport it
    bool bTaskSync = pNetworkPed->IsPosSyncSkipped();

    // C1: an NPC is being pulled out right now: let the jack animation play and keep it seated. The exit is
    // applied once the window is over (the next PED_ONFOOT of its syncer then places it where it really got out).
    bool bJackInProgress = CJackSync::IsPedJackInProgress(pNetworkPed->m_pedId);

    if (!bTaskSync && !bJackInProgress)
    {
        CVehicle* pVehicle = pPed->m_pVehicle;
        if (pVehicle && pPed->m_nPedFlags.bInVehicle && pVehicle->IsVTableValid())
        {
            // plugin::Command<Commands::TASK_LEAVE_CAR>(CPools::GetPedRef(ped->m_pPed),
            // CPools::GetVehicleRef(ped->m_pPed->m_pVehicle));
            // plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(ped->m_pPed), packet->pos.x,
            // packet->pos.y, packet->pos.z);
            pNetworkPed->RemoveFromVehicle(pVehicle);
        }
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
    if (bTaskSync || bJackInProgress)
    {
        pNetworkPed->m_bHasPosError = false;
    }
    else if (CConfigLoader::ms_bSmoothOnFoot)
    {
        // npc-velocity-sync: aim where the ped is by now, not where it was when the packet left (the walk
        // animation moves the copy; its move speed is rewritten from the animation each frame, so only the target uses it)
        CVector target = pPedOnFoot->pos;
        if (CConfigLoader::ms_bNpcVelocitySync && pPedOnFoot->hasVelocity &&
            pPedOnFoot->moveState != PEDMOVE_STILL && pPedOnFoot->moveState != PEDMOVE_NONE)
        {
            // one-way delay: half of our round trip plus about one send interval (50 ms), at most 150 ms;
            // m_vecMoveSpeed is in units per 1/50 s
            const float ageMs = std::min(CNetwork::GetRTT() * 0.5f + 50.0f, 150.0f);
            const CVector& v = pPedOnFoot->velocity;
            target += CVector(v.x, v.y, 0.0f) * (ageMs * 0.05f);
        }
        // a copy already broken (NaN) is put back on the syncer's position instead of being smoothed from it
        const CVector smoothed = IsFiniteVector(pPed->GetPosition())
            ? NetworkSmoothing::ApplyTarget(pPed->GetPosition(), target, pNetworkPed->m_vecPosError,
                pNetworkPed->m_bHasPosError, 3.0f)
            : pPedOnFoot->pos;
        if (!IsFiniteVector(smoothed))
            pNetworkPed->m_bHasPosError = false;
        pPed->SetPosn(IsFiniteVector(smoothed) ? smoothed : pPedOnFoot->pos);
    }
    else
    {
        pPed->SetPosn(pPedOnFoot->pos);
        pNetworkPed->m_bHasPosError = false;
    }

    if (!bTaskSync && !bJackInProgress)
    {
        pNetworkPed->m_fCurrentRotation = pPed->m_fCurrentRotation = pPedOnFoot->currentRotation.m_angle;
        pNetworkPed->m_fAimingRotation = pPed->m_fAimingRotation = pPedOnFoot->aimingRotation.m_angle;
        pNetworkPed->m_fLookDirection = pPed->m_fLookDirection = pPedOnFoot->lookDirection.m_angle;
    }

    pNetworkPed->ApplyAuthorityHealth(pPedOnFoot->healthSnapshot.iHealth);
    pPed->m_fArmour = pPedOnFoot->healthSnapshot.iArmour;
    pNetworkPed->ApplyRemoteDeath();

    pNetworkPed->m_vecVelocity = pPedOnFoot->velocity;
    pNetworkPed->m_nMoveState = pPedOnFoot->moveState;

    if (CUtil::IsDucked(pPed) != pPedOnFoot->bDucked)
    {
        CTaskSimpleDuckToggle task = CTaskSimpleDuckToggle(pPedOnFoot->bDucked);
        task.ProcessPed(pPed);
    }

    pNetworkPed->m_bAiming = pPedOnFoot->bAiming;
    if (pPedOnFoot->bAiming)
        pNetworkPed->m_vecAimTarget = pPedOnFoot->weaponAim;

    if (CConfigLoader::ms_bNpcAimSync && pPedOnFoot->bAiming)
    {
        // npc-aim-sync: the task is created and commanded every frame (CNetworkPed::ProcessAim)
    }
    else if (pPedOnFoot->bAiming)
    {
        CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();
        if (!useGun)
        {
            auto* taskUseGun = new CTaskSimpleUseGun(pPed->m_pTargetedObject, CVector(0.0f, 0.0f, 0.f), 1, 1, false);
            pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(taskUseGun, TASK_SECONDARY_ATTACK);
        }

        useGun = pPed->m_pIntelligence->GetTaskUseGun();

        if (useGun)
        {
            useGun->m_vecTarget = pPedOnFoot->weaponAim;
        }
    }
    else
    {
        if (auto useGun = pPed->m_pIntelligence->GetTaskUseGun())
        {
            useGun->MakeAbortable(pPed, ABORT_PRIORITY_URGENT, nullptr);
        }
    }

    pPed->m_nFightingStyle = pPedOnFoot->fightingStyle;
}

PACKET_HANDLER(ePacketType::PED_ADD_TASK, Packets::Peds::PedTaskState* pPedTaskState)
{
    if (!CConfigLoader::ms_bNpcTaskSync)
    {
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedTaskState->pedid);

    if (pNetworkPed && !pNetworkPed->m_bSyncing)
    {
        pNetworkPed->ApplyTaskState(*pPedTaskState);
    }
}

PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    if (CNetworkPed* pDriver = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid))
    {
        if (pDriver->m_bStreamedOut)
        {
            pDriver->m_vecLastPos = pPedDriverUpdate->pos;
            pDriver->m_fHealth = pPedDriverUpdate->pedHealth.iHealth;
            if (!pNetworkVehicle->m_bStreamedOut)
                pDriver->StreamIn();
        }
    }

    if (pNetworkVehicle->m_bStreamedOut)
    {
        pNetworkVehicle->m_vecLastPos = pPedDriverUpdate->pos;
        pNetworkVehicle->m_fLastHealth = pPedDriverUpdate->health;
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid() || pVehicle->m_matrix == nullptr)
    {
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (pPed == nullptr || !pPed->IsVTableValid())
    {
        return;
    }

    // a carriage's motion belongs to TRAIN_STATE: its cosmetic NPC driver goes before the warp below
    if (Packets::Trains::IsTrainModel(pNetworkVehicle->m_nModelId))
        CTrainSync::EvictCosmeticDriver(pVehicle);

    // while the enter / leave car task runs the copy gets in by itself (npc-task-sync)
    bool bTaskSync = pNetworkPed->IsPosSyncSkipped();
    if (!bTaskSync && !CJackSync::IsPedJackInProgress(pNetworkPed->m_pedId) &&
        (pPed->m_pVehicle != pVehicle || !pPed->m_nPedFlags.bInVehicle))
    {
        pNetworkPed->WarpIntoVehicleDriver(pVehicle);
    }

    // a carriage's transform is TRAIN_STATE's: the seat above applied, the rest below does not
    if (Packets::Trains::IsTrainModel(pNetworkVehicle->m_nModelId))
        return;

    pNetworkVehicle->ApplyNetworkPosition(pPedDriverUpdate->pos);
    pNetworkVehicle->ApplyNetworkRotation(pPedDriverUpdate->roll, pPedDriverUpdate->rot);
    pNetworkPed->m_vecVelocity = pPedDriverUpdate->velocity;
    pVehicle->m_vecMoveSpeed = pPedDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pPedDriverUpdate->turnSpeed;

    pNetworkPed->ApplyWeaponSnapshot(pPedDriverUpdate->pedWeapon);

    pNetworkPed->ApplyAuthorityHealth(pPedDriverUpdate->pedHealth.iHealth);
    pPed->m_fArmour = pPedDriverUpdate->pedHealth.iArmour;

    pVehicle->m_nPrimaryColor = pPedDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pPedDriverUpdate->color2;

    pVehicle->m_fHealth = pPedDriverUpdate->health;

    if (pNetworkVehicle->m_nPaintJob != pPedDriverUpdate->paintjob)
    {
        pVehicle->SetRemap(pPedDriverUpdate->paintjob);
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pPedDriverUpdate->bikeLean;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;
        pPlane->m_fLandingGearStatus = pPedDriverUpdate->planeGearState;
    }
    else if (pVehicle->m_nVehicleSubType == VEHICLE_HELI)
    {
        pNetworkVehicle->m_bRemoteRotorOn = pPedDriverUpdate->planeGearState > 0.0f;
        pNetworkVehicle->m_bRemoteSearchLight = pPedDriverUpdate->planeGearState >= 2.0f;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_BMX)
    {
        CBmx* pBmx = (CBmx*)pVehicle;
        pBmx->m_fControlPedaling = pPedDriverUpdate->controlPedaling;
    }
    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        ((CAutomobile*)pVehicle)->m_wMiscComponentAngle = pPedDriverUpdate->miscComponentAngle;
    }
    pVehicle->m_eDoorLock = pPedDriverUpdate->locked;

    pVehicle->m_fGasPedal = pNetworkPed->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pVehicle->m_fBreakPedal = pNetworkPed->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pVehicle->m_fSteerAngle = pNetworkPed->m_fSteerAngle = pPedDriverUpdate->steerAngle;

    pVehicle->m_nVehicleFlags.bSirenOrAlarm = pPedDriverUpdate->siren;
    if (pPedDriverUpdate->horn && pVehicle->m_nHornCounter == 0)
    {
        pVehicle->PlayCarHorn();
    }
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);

    if (pNetworkVehicle == nullptr || pNetworkPed == nullptr)
        return;

    if (pNetworkPed->m_bStreamedOut)
    {
        pNetworkPed->m_vecLastPos = pNetworkVehicle->m_bStreamedOut || !pNetworkVehicle->m_pVehicle
            ? pNetworkVehicle->m_vecLastPos
            : pNetworkVehicle->m_pVehicle->GetPosition();
        pNetworkPed->m_fHealth = pPedPassengerSync->healthSnapshot.iHealth;
        if (!pNetworkVehicle->m_bStreamedOut)
            pNetworkPed->StreamIn();
    }

    if (pNetworkVehicle->m_pVehicle == nullptr)
        return;

    if (pNetworkPed->m_pPed == nullptr)
        return;

    if (!pNetworkVehicle->m_pVehicle->IsVTableValid() || !pNetworkPed->m_pPed->IsVTableValid())
        return;

    CPed* pPed = pNetworkPed->m_pPed;
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    int8_t seatid = pPedPassengerSync->seatid;
    // also when it sits in another vehicle or another seat (it changed seat or the warp was missed)
    bool inRightSeat = pPed->m_nPedFlags.bInVehicle && pPed->m_pVehicle == pVehicle && seatid >= 0 &&
                       seatid < pVehicle->m_nMaxPassengers && seatid < 8 && pVehicle->m_apPassengers[seatid] == pPed;

    // while the enter / leave car task runs the copy gets in by itself (npc-task-sync)
    bool bTaskSync = pNetworkPed->IsPosSyncSkipped();
    // the game does not check the seat before warping (it writes out of m_apPassengers / uses an invalid door)
    const bool bValidSeat = seatid >= 0 && seatid < 8 && seatid < pVehicle->m_nMaxPassengers;
    if (!bTaskSync && !CJackSync::IsPedJackInProgress(pNetworkPed->m_pedId) && !inRightSeat && bValidSeat)
    {
        pNetworkPed->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pPedPassengerSync->seatid);
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedPassengerSync->weaponSnapshot);

    pNetworkPed->ApplyAuthorityHealth(pPedPassengerSync->healthSnapshot.iHealth);
    pNetworkPed->m_pPed->m_fArmour = pPedPassengerSync->healthSnapshot.iArmour;
}

PACKET_HANDLER(ePacketType::PED_JACKED, Packets::Peds::PedJacked* pPedJacked)
{
    // its syncer keeps sending driver/passenger updates until it reacts: never warp it back meanwhile
    CJackSync::SuppressPedWarp(pPedJacked->pedid);
    CJackSync::ApplyPedJacked(pPedJacked->pedid, pPedJacked->vehicleid, pPedJacked->seatid, pPedJacked->door,
        pPedJacked->thiefPlayerId);
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);

    if (pNetworkPed && pNetworkPed->m_pPed)
    {
        CPed* pPed = pNetworkPed->m_pPed;

        // npc-aim-sync: the gun task of the copy plays this shot (its own bullet is cut by CWeapon__Fire_Hook)
        if (CConfigLoader::ms_bNpcAimSync && !pNetworkPed->m_bSyncing)
        {
            pNetworkPed->m_bFireRequested = true;
            pNetworkPed->m_vecAimTarget = pPedShotSync->target;
            pNetworkPed->m_nShotAimUntil = (CConfigLoader::GetSyncTime() + 400) | 1;
        }
        // npc-driveby-sync: the drive-by of a copy in a vehicle stays up between the shots of a burst
        if (CConfigLoader::ms_bNpcDriveBySync && !pNetworkPed->m_bSyncing && pPed->m_nPedFlags.bInVehicle)
        {
            pNetworkPed->m_vecAimTarget = pPedShotSync->target;
            pNetworkPed->m_nDriveByUntil = (CConfigLoader::GetSyncTime() + 1500) | 1;
        }

        if (pPed->GetWeapon().m_eWeaponType != pPedShotSync->weaponType)
        {
            pPed->SetCurrentWeapon(pPedShotSync->weaponType);
        }

        // Fire does nothing without ammo and a ready state
        CWeapon& weapon = pPed->GetWeapon();
        if (weapon.m_eWeaponType == pPedShotSync->weaponType)
        {
            if (weapon.m_nAmmoInClip < 1)
                weapon.m_nAmmoInClip = 1;
            if (weapon.m_nTotalAmmo < 1)
                weapon.m_nTotalAmmo = 1;
            weapon.m_nState = WEAPONSTATE_READY;
        }

        bool fired = weapon.Fire(
            pPed, &pPedShotSync->origin, &pPedShotSync->effect, nullptr, &pPedShotSync->target, nullptr);

        if (!fired)
        {
            CWeaponInfo* pWeaponInfo = CWeaponInfo::GetWeaponInfo(pPedShotSync->weaponType, WEAPSKILL_STD);
            if (pWeaponInfo && pWeaponInfo->m_nWeaponFire == WEAPON_FIRE_INSTANT_HIT)
            {
                CBulletTraces::AddTrace(
                    &pPedShotSync->origin, &pPedShotSync->target, (int)pPedShotSync->weaponType, pPed);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay)
{
    // CChat::AddMessage("PedSay %d %d %d %d %d", pPedSay->phraseId, pPedSay->startTimeDelay, pPedSay->overrideSilence,
    // pPedSay->isForceAudible, pPedSay->isFrontEnd);

    CPed* pPed = (CPed*)pPedSay->entity.GetEntity();
    if (pPed)
    {
        // CAEPedSpeechAudioEntity::AddSayEvent
        plugin::CallMethodAndReturn<int16_t, 0x4E6550, CAEPedSpeechAudioEntity*, int, int16_t, uint32_t, float, bool,
            bool, bool>(&pPed->m_pedSpeech, AE_SPEECH_PED, pPedSay->phraseId, pPedSay->startTimeDelay, 1.0f,
            pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd);
    }
}


PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims)
{
    if (auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid))
    {
        if (!pNetworkPed->m_bSyncing)
        {
            pNetworkPed->m_bClaimOnRelease = false;
        }
    }
}

PACKET_HANDLER(ePacketType::PED_DAMAGE, Packets::Peds::PedDamage* pPedDamage)
{
    CPedDamageSync::HandleDamage(*pPedDamage);
}

PACKET_HANDLER(ePacketType::PED_HIT_PLAYER, Packets::Peds::PedHitPlayer* pPedHitPlayer)
{
    CPedDamageSync::HandleHitPlayer(*pPedHitPlayer);
}

PACKET_HANDLER(ePacketType::PED_ANIM_SET, Packets::Peds::PedAnimSet* pPedAnimSet)
{
    if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedAnimSet->pedid))
    {
        pNetworkPed->ApplyAnimSet(*pPedAnimSet);
    }
}
