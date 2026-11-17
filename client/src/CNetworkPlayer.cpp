#include "stdafx.h"
#include <CTaskSimpleCarSetPedOut.h>
#include "CMissionMirror.h"
#include "CJackSync.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CTaskComplexDie.h>
#include <eAnimations.h>

CNetworkPlayer::~CNetworkPlayer()
{
    if (m_pPed)
    {
        m_pPed->CleanUpOldReference((CEntity**)&m_pPed);
        DestroyPed();
    }
}

CNetworkPlayer::CNetworkPlayer(playerid_t id, CVector position)
{
    m_playerId = id;
    memset(m_anChainProgress, Packets::Scripts::PlayerMissionMode::CHAIN_UNKNOWN, sizeof(m_anChainProgress));
    m_nSlot = CNetworkPlayerManager::GetFreeSlot();
    COOP_VERIFY(m_nSlot != -1);

    m_pPedClothesDesc.SetTextureAndModel("VEST", "VEST", 0);
    m_pPedClothesDesc.SetTextureAndModel("JEANSDENIM", "JEANS", 2);
    m_pPedClothesDesc.SetTextureAndModel("SNEAKERBINCBLK", "SNEAKER", 3);
    m_pPedClothesDesc.SetTextureAndModel("PLAYER_FACE", "HEAD", 1);

    CreatePed(position);
}

void CNetworkPlayer::SetKeys(const Packets::Players::SKeySnapshot& keys)
{
    m_rawOldControllerState = keys.oldControllerState;
    m_rawNewControllerState = keys.newControllerState;

    if (keys.controlsDisabled)
    {
        m_oldControllerState = CControllerState{};
        m_newControllerState = CControllerState{};
        return;
    }

    m_oldControllerState = keys.oldControllerState;
    m_newControllerState = keys.newControllerState;
}

bool CNetworkPlayer::CreatePed(CVector position)
{
    if (m_pPed != nullptr)
    {
        return true;  // already created (a failed creation leaves it null and is retried by the on-foot update)
    }

    unsigned int actorId = 0;
    int playerId = m_nSlot + 2;

    // TODO: get rid of the ugly scm command and call in-game funcs
    plugin::Command<Commands::CREATE_PLAYER>(playerId, position.x, position.y, position.z, &actorId);
    plugin::Command<Commands::GET_PLAYER_CHAR>(playerId, &actorId);

    // if the player pool is full the commands fail and CPools::GetPed(0) would return the ped of another slot
    // (possibly the local player): never dereference that
    CPlayerPed* ped = actorId != 0 ? (CPlayerPed*)CPools::GetPed((int)actorId) : nullptr;
    if (ped == nullptr || !ped->IsVTableValid())
    {
        logger::warn("CNetworkPlayer::CreatePed: could not create the ped of the slot %d", playerId);
        return false;
    }

    // it must be the ped the game assigned to our slot, and it must not be a ped that already belongs to someone
    if (CWorld::Players[playerId].m_pPed != ped || ped == FindPlayerPed(0) ||
        CNetworkPlayerManager::GetPlayer((CEntity*)ped) != nullptr)
    {
        logger::warn("CNetworkPlayer::CreatePed: the ped of the slot %d is not ours, not using it", playerId);
        return false;
    }

    m_pPed = ped;
    m_pPed->RegisterReference((CEntity**)&m_pPed);
    m_pPed->m_fMaxHealth = std::max(100.0f, m_stats[STAT_MAX_HEALTH] / 5.69f);

    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);

    // set player immunies, he doesn't care about the pain now
    Command<Commands::SET_CHAR_PROOFS>(actorId, 0, 1, 1, 0, 0);

    if (m_pPed->m_pPlayerData != nullptr && m_pPed->m_pPlayerData->m_pPedClothesDesc != nullptr)
    {
        *m_pPed->m_pPlayerData->m_pPedClothesDesc = m_pPedClothesDesc;
    }

    CMissionMirror::LogEventf("net-rebuild-create cutsceneRunning=%d loadStatus=%d", CCutsceneMgr::ms_running ? 1 : 0, (int)CCutsceneMgr::ms_cutsceneLoadStatus);
    CClothes::RebuildPlayer(m_pPed, false);

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;

    return true;
}

void CNetworkPlayer::DestroyPed()
{
    if (m_pPed == nullptr)
    {
        return;
    }

    CPlayerPed* ped = m_pPed;
    int internalId = GetInternalId();
    CVehicle* vehicle = ped->m_pVehicle;

    if (vehicle != nullptr && vehicle->IsVTableValid())
    {
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(ped), 0.f, 0.f, 0.f);
    }

    // a seat (or the driver slot) still pointing at the ped would be a dangling reference after the delete, even
    // when bInVehicle went out of sync with the vehicle arrays
    if (vehicle != nullptr && vehicle->IsVTableValid())
    {
        if (vehicle->m_pDriver == ped)
        {
            vehicle->m_pDriver = nullptr;
        }

        for (int i = 0; i < 8; i++)
        {
            if (vehicle->m_apPassengers[i] == ped)
            {
                vehicle->m_apPassengers[i] = nullptr;
            }
        }
    }

    // abort the tasks that may keep a pointer to the ped (or to the vehicle it was entering)
    if (ped->m_pIntelligence != nullptr)
    {
        ped->m_pIntelligence->FlushImmediately(false);
    }

    CWorld::RemoveReferencesToDeletedObject(ped);

    if (m_pPed != nullptr)
    {
        ped->CleanUpOldReference((CEntity**)&m_pPed);
    }
    m_pPed = nullptr;

    // a ped in a local player slot is not ours: drop the pointer instead of deleting the local player's ped
    if (internalId >= 0 && internalId < 2)
    {
        logger::error("CNetworkPlayer::DestroyPed: refusing to delete a ped of the local slot %d", internalId);
        return;
    }

    if (ped->IsVTableValid())
    {
        CWorld::Remove(ped);
        delete ped;
    }

    // the game doesn't clear CWorld::Players[n].m_pPed when the ped is deleted
    if (internalId >= 2)
    {
        CWorld::Players[internalId].m_pPed = nullptr;
    }
}

void CNetworkPlayer::Respawn(const Packets::Players::RespawnPlayer& packet)
{
    // a respawn always ends the death/arrest state
    m_bDead = false;
    m_bBusted = false;
    m_looksDeadSince = 0;
    m_bHasPosError = false;
    m_vecPosError = CVector(0.0f, 0.0f, 0.0f);

    CVector position = packet.vecPos;
    if (position.x == 0.0f && position.y == 0.0f && position.z == 0.0f)
    {
        // a short packet (or an unknown position): use the last on-foot position as before
        position = m_onFootSnapshotInterpolated.vecPos;
    }

    if (m_pPed != nullptr)
    {
        DestroyPed();
    }

    if (CreatePed(position))
    {
        m_pPed->m_nAreaCode = packet.areaCode;
    }

    m_onFootSnapshotInterpolated.vecPos = position;
}

void CNetworkPlayer::HandleDeath(const Packets::Players::PlayerDeath& packet)
{
    if (m_pPed == nullptr || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_bDead)
    {
        return;  // repeated packet
    }
    m_bDead = true;
    m_deadSince = GetTickCount();
    m_bBusted = packet.deathState == Packets::Players::PlayerDeath::BUSTED;
    m_looksDeadSince = 0;

    // let go of the seat first: the driver/passenger updates still in flight must not warp the corpse back into
    // the vehicle it died in
    if (m_pPed->m_pVehicle != nullptr && m_pPed->m_pVehicle->IsVTableValid())
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_bHasPosError = false;
    m_vecPosError = CVector(0.0f, 0.0f, 0.0f);

    m_onFootSnapshotInterpolated.vecPos = packet.pos;

    m_pPed->SetPosn(packet.pos);
    m_pPed->m_fCurrentRotation = packet.heading.m_angle;
    m_pPed->m_fAimingRotation = packet.heading.m_angle;

    if (m_pPed->m_pIntelligence != nullptr)
    {
        m_pPed->m_pIntelligence->FlushImmediately(false);
    }

    if (packet.deathState == Packets::Players::PlayerDeath::BUSTED)
    {
        // arrested: the ped is alive, he just stands with the hands up until the respawn
        m_pPed->SetPedState(PEDSTATE_ARRESTED);
        return;
    }

    m_pPed->m_fHealth = 0.0f;
    m_pPed->m_fArmour = 0.0f;
    m_onFootSnapshotInterpolated.healthSnapshot.iHealth = 0;
    m_onFootSnapshotInterpolated.healthSnapshot.iArmour = 0;

    // the game plays the same task on the local player when he dies; ANIM_GROUP_DEFAULT is the ped/player ifp
    auto task = new CTaskComplexDie((eWeaponType)packet.weaponType, ANIM_GROUP_DEFAULT, ANIM_DEFAULT_KO_SHOT_FRONT_0,
        4.0f, 1.0f, false, false, 0, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, true);
}

int CNetworkPlayer::GetInternalId()  // most used for CWorld::PlayerInFocus
{
    byte playerNumber = 0;

    // a null ped would match any unused slot
    if (!m_pPed)
    {
        return -1;
    }

    for (; playerNumber < Config::MAX_SERVER_PLAYERS + 2; playerNumber++)
    {
        if (m_pPed == CWorld::Players[playerNumber].m_pPed)
        {
            return playerNumber;
        }
    }

    return -1;
}

std::string CNetworkPlayer::GetName()
{
    if (m_Name[0] == '\0')
    {
        char buffer[32 + 1];
        sprintf(buffer, "player %u", m_playerId.value());
        return buffer;
    }

    return m_Name;
}

char CNetworkPlayer::GetWeaponSkill(eWeaponType weaponType)
{
    if (weaponType < WEAPON_PISTOL || weaponType > WEAPON_TEC9)
        return 1;

    eStats weaponStatId = plugin::CallAndReturn<eStats, 0x743CD0>(weaponType);  // CWeaponInfo::GetSkillStatIndex
    int statSyncId = CStatsSync::GetSyncIdByInternal(weaponStatId);
    float weaponStat = m_stats[weaponStatId];

    if (CWeaponInfo::GetWeaponInfo(weaponType, 2)->m_nReqStatLevel <= weaponStat)
        return 2;

    return CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nReqStatLevel <= weaponStat;
}

void CNetworkPlayer::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (vehicle == nullptr || !vehicle->IsVTableValid() || !m_pPed->IsVTableValid() || m_bDead)
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);

    m_onFootSnapshotInterpolated.vecPos = vehicle->GetPosition();
}

void CNetworkPlayer::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (vehicle == nullptr || !vehicle->IsVTableValid() || !m_pPed->IsVTableValid() || m_bDead ||
        seatid < 0 || seatid >= 8 || seatid >= vehicle->m_nMaxPassengers)
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    if (doorId < 0)
    {
        return;
    }

    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);

    m_onFootSnapshotInterpolated.vecPos = vehicle->GetPosition();
}

void CNetworkPlayer::EnterVehiclePassenger(CVehicle* vehicle, int seatid)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (vehicle == nullptr || !vehicle->IsVTableValid() || !m_pPed->IsVTableValid() || m_bDead ||
        seatid < 0 || seatid >= 8 || seatid >= vehicle->m_nMaxPassengers)
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    if (doorId < 0)
    {
        return;
    }

    auto task = new CTaskComplexEnterCarAsPassenger(vehicle, doorId, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, 3, false);
}

void CNetworkPlayer::RemoveFromVehicle(CVehicle* vehicle)
{
    COOP_VERIFY(m_pPed != nullptr);

    if (vehicle == nullptr || !vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);
    CUtil::ReleaseStackCarTask(task);
}

void CNetworkPlayer::HandleTask(Packets::Players::SetPlayerTask& packet)
{
    if (!m_pPed || m_bDead || CJackSync::IsPlayerJackInProgress(m_pPed))
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("HandleTask %d toggle %d", packet.taskType, packet.toggle);
#endif

    m_pPed->SetPosn(packet.vecPos);
    m_pPed->m_fCurrentRotation = packet.currentRotation.m_angle;
    m_pPed->m_fAimingRotation = packet.aimingRotation.m_angle;

    CTask* activeTask = m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
    eTaskType activeTaskType = activeTask ? activeTask->GetTaskType() : TASK_NONE;
    switch ((eTaskType)packet.taskType)
    {
        case eTaskType::TASK_COMPLEX_JUMP:
        {
            m_pPed->ClearWeaponTarget();
            if (activeTask && activeTaskType == packet.taskType)  // if the jump task is active
            {
                return;
            }

            if (m_pPed->m_pIntelligence->GetTaskDuck(false))
            {
                CTaskSimpleDuckToggle(0).ProcessPed(m_pPed);
            }

            m_pPed->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexJump(0), 3, false);

            break;
        }
        case eTaskType::TASK_SIMPLE_DUCK:
        {
            m_bRequestedDuckTask = true;
            // if (packet.toggle)
            //{
            //     m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);

            //    CTaskSimpleFight* pFightingTask = m_pPed->m_pIntelligence->GetTaskFighting();
            //    if (pFightingTask)
            //    {
            //        // abort fighting task (0x6879C2)
            //        plugin::CallMethodAndReturn<bool, 0x61C5E0, CTaskSimpleFight*, CEntity*, int8_t>(pFightingTask,
            //        nullptr, 18);
            //    }
            //}
            // else
            //{
            //    m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
            //}
            break;
        }
    }
}

void CNetworkPlayer::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr || m_bDead)
    {
        return;
    }

    m_onFootSnapshotInterpolated.weaponSnapshot = weaponSnapshot;
    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}

void CNetworkPlayer::NoteNetState(int health, int armour, const Packets::Players::SWeaponSnapshot* weapon, const CVector* footPos)
{
    m_netAliveTick = GetTickCount();
    m_netHealth = health;
    m_netArmour = armour;
    if (weapon != nullptr)
    {
        m_netWeapon = *weapon;
    }
    m_bNetOnFoot = footPos != nullptr;
    if (footPos != nullptr)
    {
        m_vecNetFootPos = *footPos;
    }
}

bool CNetworkPlayer::LooksDeadButAlive() const
{
    if (m_pPed == nullptr || !m_pPed->IsVTableValid() || m_bBusted)
    {
        return false;
    }

    // the network must say alive with an update that is recent, and after the death packet if there was one
    uint32_t now = GetTickCount();
    if (m_netHealth < 1 || m_netAliveTick == 0 || now - m_netAliveTick > 2500)
    {
        return false;
    }
    if (m_bDead && (int32_t)(m_netAliveTick - m_deadSince) < 1500)
    {
        return false;
    }

    // the copy: death state kept by us, zero health, or the game's dying/dead ped state (corpse pose, frozen)
    return m_bDead || m_pPed->m_fHealth <= 0.0f || m_pPed->m_ePedState == PEDSTATE_DIE || m_pPed->m_ePedState == PEDSTATE_DEAD;
}

bool CNetworkPlayer::Refresh(bool force)
{
    if (!LooksDeadButAlive())
    {
        return false;
    }

    // never during a cutscene, never with the local ped in the slot, never in the middle of a native jack
    if (CCutsceneMgr::ms_running)
    {
        return false;
    }
    if (GetInternalId() < 2 || CJackSync::IsPlayerJackInProgress(m_pPed))
    {
        return false;
    }
    (void)force;

    CVector position = m_bNetOnFoot ? m_vecNetFootPos : m_pPed->GetPosition();
    uint8_t areaCode = m_pPed->m_nAreaCode;

    // a corpse cannot be revived cleanly: the existing respawn path builds a fresh ped
    m_bDead = false;
    m_bBusted = false;
    m_looksDeadSince = 0;
    m_bHasPosError = false;
    m_vecPosError = CVector(0.0f, 0.0f, 0.0f);

    DestroyPed();
    if (!CreatePed(position))
    {
        return true;  // the next on-foot update retries the creation
    }

    m_pPed->m_nAreaCode = areaCode;
    m_pPed->m_fHealth = (float)m_netHealth;
    m_pPed->m_fArmour = (float)m_netArmour;
    m_onFootSnapshotInterpolated.vecPos = position;
    m_onFootSnapshotInterpolated.healthSnapshot.iHealth = (uint8_t)m_netHealth;
    m_onFootSnapshotInterpolated.healthSnapshot.iArmour = (uint8_t)m_netArmour;
    ApplyWeaponSnapshot(m_netWeapon);  // a vehicle update puts him back in his car
    CMissionMirror::LogEventf("remote-refresh player=%d", (int)m_playerId.value());
    return true;
}
