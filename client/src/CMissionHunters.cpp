#include "stdafx.h"
#include "CMissionHunters.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CConfigLoader.h"
#include "CTaskComplexKillPedOnFoot.h"

static constexpr float MAX_SWITCH_DISTANCE = 40.0f;
static constexpr float SWITCH_RATIO = 0.7f;
static constexpr float IDLE_ENGAGE_DISTANCE = 25.0f;

static bool IsHuntable(CPed* ped, unsigned char areaCode)
{
    return ped && ped->m_fHealth > 0.0f && ped->m_nAreaCode == areaCode;
}

// the player ped this ped is hunting: the local one or a network player's
static bool IsPlayerPed(CPed* ped)
{
    if (!ped)
        return false;
    if (ped == FindPlayerPed(0))
        return true;
    return CNetworkPlayerManager::GetPlayer(ped) != nullptr;
}

// the ped hates players: its own acquaintances (SET_CHAR_RELATIONSHIP) or its ped type's (SET_RELATIONSHIP, ped.dat);
// CPedType::GetPedTypeAcquaintances(id, type) 0x6089D0, ACQUAINTANCE_HATE 4, PED_TYPE_PLAYER1 flag 1 (gta-reversed)
static bool HatesPlayers(CPed* ped)
{
    const unsigned int typeHate = plugin::CallAndReturn<unsigned int, 0x6089D0, int, int>(4, ped->m_nPedType);
    return ((ped->m_acquaintance.m_nHate | typeHate) & 1) != 0;
}

// nearest player of the mission that is not the local one (the ped's syncer)
static CPed* FindNearestGuest(CPed* ped, float& outDistance)
{
    CPed* best = nullptr;
    outDistance = FLT_MAX;
    const CVector& pos = ped->GetPosition();
    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!IsHuntable(networkPlayer->m_pPed, ped->m_nAreaCode) || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;
        const float distance = (networkPlayer->m_pPed->GetPosition() - pos).Magnitude();
        if (distance < outDistance)
        {
            best = networkPlayer->m_pPed;
            outDistance = distance;
        }
    }
    return best;
}

// A hostile mission ped that is not attacking anyone yet only reacts to the script (which watches $PLAYER_ACTOR, the
// host) or to damage: a guest walking up to it was ignored until he shot. When a guest is close, in sight and clearly
// nearer than the local player, the ped goes after him with the same command the scripts use.
static void EngageIdleHostile(CPed* ped, CPlayerPed* localPlayer)
{
    if (ped->m_nPedFlags.bInVehicle || !HatesPlayers(ped))
        return;

    // only a ped left to its default behaviour: a task the script gave it (sequence, ambush, stay put, duck...) or an
    // event response in progress (fighting, fleeing) is left alone
    auto& taskMgr = ped->m_pIntelligence->m_TaskMgr;
    if (taskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY] || taskMgr.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP] ||
        taskMgr.m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_NONTEMP] || taskMgr.m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE])
        return;

    float guestDistance;
    CPed* guest = FindNearestGuest(ped, guestDistance);
    if (!guest || guestDistance > IDLE_ENGAGE_DISTANCE)
        return;

    const CVector& pos = ped->GetPosition();
    const float localDistance = IsHuntable(localPlayer, ped->m_nAreaCode) ? (localPlayer->GetPosition() - pos).Magnitude() : FLT_MAX;
    if (guestDistance > localDistance * SWITCH_RATIO)
        return;

    // eyes to the guest's chest, buildings and vehicles only (what blocks the sight)
    CVector eyes = pos + CVector(0.0f, 0.0f, 0.7f);
    CVector chest = guest->GetPosition() + CVector(0.0f, 0.0f, 0.5f);
    if (!CWorld::GetIsLineOfSightClear(eyes, chest, true, true, false, false, false, false, false))
        return;

    Command<Commands::TASK_KILL_CHAR_ON_FOOT>(CPools::GetPedRef(ped), CPools::GetPedRef(guest));
    CMissionMirror::LogEventf("hunter-engage model=%d guest=%.1f local=%.1f", ped->m_nModelIndex, guestDistance, localDistance);
}

void CMissionHunters::Process()
{
    if (!CConfigLoader::ms_bMissionHunters || CCoopMission::IsLocalIndividualMission() || CCoopMission::HasLocalLeft())
        return;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    for (CNetworkPed* networkPed : CNetworkPedManager::m_pPeds)
    {
        CPed* ped = networkPed->m_pPed;
        if (!networkPed->m_bSyncing || !ped || ped->m_nCreatedBy != 2 || ped->m_fHealth <= 0.0f || !ped->m_pIntelligence)
            continue;

        auto* task = static_cast<CTaskComplexKillPedOnFoot*>(
            ped->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_KILL_PED_ON_FOOT));
        if (!task)
        {
            EngageIdleHostile(ped, localPlayer);
            continue;
        }
        if (!IsPlayerPed(task->m_pTarget))
            continue;

        const CVector& pos = ped->GetPosition();
        CPed* target = task->m_pTarget;
        const float targetDistance = (target->GetPosition() - pos).Magnitude();

        // nearest player of the mission, the local one included
        CPed* best = IsHuntable(localPlayer, ped->m_nAreaCode) ? localPlayer : nullptr;
        float bestDistance = best ? (localPlayer->GetPosition() - pos).Magnitude() : FLT_MAX;
        for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (!IsHuntable(networkPlayer->m_pPed, ped->m_nAreaCode) || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
                continue;
            const float distance = (networkPlayer->m_pPed->GetPosition() - pos).Magnitude();
            if (distance < bestDistance)
            {
                best = networkPlayer->m_pPed;
                bestDistance = distance;
            }
        }

        if (!best || best == target || bestDistance > MAX_SWITCH_DISTANCE || bestDistance > targetDistance * SWITCH_RATIO)
            continue;

        Command<Commands::TASK_KILL_CHAR_ON_FOOT>(CPools::GetPedRef(ped), CPools::GetPedRef(best));
        CMissionMirror::LogEventf("hunter-retarget model=%d from=%.1f to=%.1f", ped->m_nModelIndex, targetDistance, bestDistance);
    }
}
