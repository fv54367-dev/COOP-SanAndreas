#include "stdafx.h"
#include "CMissionPhotos.h"
#include "CPedDamageSync.h"
#include "CBulletImpactForceGuard.h"
#include "PlayerHooks.h"
#include "CKeySync.h"
#include "CAimSync.h"
#include "CJackSync.h"
#include <game_sa/CPedDamageResponseInfo.h>
#include <CPacketBuffer.h>
#include <CServerTime.h>
#include <CCoopMission.h>
#include <COpCodeSync.h>
#include <CMissionMirror.h>
#include <CPedGroups.h>
#include <CNetworkPed.h>

// C5: CReferences::RemoveReferencesToPlayer is called before the game (main.scm) moves the ped to the hospital or
// the police station, so the respawn packet is sent a few frames later from the local player's ProcessControl with
// the real position
static bool g_bRespawnPending = false;
static int g_nRespawnWaitFrames = 0;
static bool g_bLocalDeathReported = false;
static CVector g_vecLocalDeathPos;

static void SendLocalPlayerDeath(CPlayerPed* localPlayer)
{
    if (!CNetwork::m_bAuthenticated)
    {
        return;
    }

    Packets::Players::PlayerDeath packet{};
    packet.pos = localPlayer->GetPosition();
    packet.heading = localPlayer->m_fCurrentRotation;
    packet.deathState = CWorld::Players[0].m_nPlayerState == ePlayerState::PLAYERSTATE_HASBEENARRESTED
        ? Packets::Players::PlayerDeath::BUSTED
        : Packets::Players::PlayerDeath::DIED;

    // the means of death picks the death animation on the other clients
    eWeaponType weaponType = localPlayer->m_aWeapons[localPlayer->m_nActiveWeaponSlot].m_eWeaponType;
    packet.weaponType = weaponType <= WEAPON_FLARE ? weaponType : WEAPON_UNARMED;

    // -1 on foot, 0 for the driver, 1..7 for the passenger slots, so the others know he died inside a vehicle
    CVehicle* vehicle = localPlayer->m_pVehicle;
    if (vehicle != nullptr && vehicle->IsVTableValid() && localPlayer->m_nPedFlags.bInVehicle)
    {
        if (vehicle->m_pDriver == localPlayer)
        {
            packet.seatid = 0;
        }
        else
        {
            for (int i = 0; i < 8; i++)
            {
                if (vehicle->m_apPassengers[i] == localPlayer)
                {
                    packet.seatid = (int8_t)(i + 1);
                    break;
                }
            }
        }
    }

    GetPacketFactory().Send(packet);
    g_vecLocalDeathPos = packet.pos;
}

static void SendLocalPlayerRespawn(CPlayerPed* localPlayer)
{
    Packets::Players::RespawnPlayer packet{};
    packet.vecPos = localPlayer->GetPosition();
    packet.areaCode = localPlayer->m_nAreaCode;
    GetPacketFactory().Send(packet);
}

static void ProcessLocalPlayerLifeState(CPlayerPed* localPlayer)
{
    if (g_bRespawnPending)
    {
        // the respawn point is set a few frames after the hook (main.scm warps the ped): wait before sending it
        g_nRespawnWaitFrames++;

        bool alive = localPlayer->m_fHealth > 0.0f;
        bool moved = DistanceBetweenPoints(localPlayer->GetPosition(), g_vecLocalDeathPos) > 5.0f;

        if ((alive && (moved || g_nRespawnWaitFrames >= 30)) || g_nRespawnWaitFrames > 180)
        {
            SendLocalPlayerRespawn(localPlayer);

            g_bRespawnPending = false;
            g_nRespawnWaitFrames = 0;
            if (alive)
            {
                g_bLocalDeathReported = false;
            }
            return;
        }
    }

    bool dead = localPlayer->m_fHealth <= 0.0f ||
        CWorld::Players[0].m_nPlayerState == ePlayerState::PLAYERSTATE_HASDIED ||
        CWorld::Players[0].m_nPlayerState == ePlayerState::PLAYERSTATE_HASBEENARRESTED;

    if (!dead)
    {
        if (!g_bRespawnPending)
        {
            if (g_bLocalDeathReported)
            {
                // alive again without CReferences::RemoveReferencesToPlayer (a script healed/revived him)
                SendLocalPlayerRespawn(localPlayer);
            }
            g_bLocalDeathReported = false;
        }
        return;
    }

    if (g_bLocalDeathReported)
    {
        return;
    }

    g_bLocalDeathReported = true;
    SendLocalPlayerDeath(localPlayer);
}

static void __fastcall CPlayerPed__ProcessControl_Hook(CPlayerPed* This)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);

    if (This == localPlayer)
    {
        ProcessLocalPlayerLifeState(localPlayer);
        patch::SetRaw(0x6884C4, (void*)"\xD9\x96\x5C\x05\x00\x00", 6, false);
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        patch::Nop(0x6884C4, 6, false);
        return;
    }

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(This);

    if (player == nullptr)
    {
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        return;
    }

    int playerNum = player->GetInternalId();

    if (playerNum == -1)
    {
        plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
        return;
    }

    if (player->m_bHasPosError)
    {
        if (CConfigLoader::ms_bSmoothOnFoot && !This->m_nPedFlags.bInVehicle &&
            !CJackSync::IsPlayerJackInProgress(This))
            This->SetPosn(This->GetPosition() + NetworkSmoothing::Step(player->m_vecPosError, player->m_bHasPosError));
        else
            player->m_bHasPosError = false;
    }

    CWorld::PlayerInFocus = playerNum;

    CKeySync::ApplyNetworkPlayerContext(player);
    CAimSync::ApplyNetworkPlayerContext(player);
    // CStatsSync::ApplyNetworkPlayerContext(player);

    // if (CPad::GetPad(0)->NewState.RightShoulder1) // is aiming
    //{
    //     player->m_pPed->m_fCurrentRotation = player->m_playerOnFoot.currentRotation;
    // }
    // player->m_pPed->m_fAimingRotation = player->m_playerOnFoot.aimingRotation;

    player->m_pPed->m_fHealth = player->m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
    player->m_pPed->m_fArmour = player->m_onFootSnapshotInterpolated.healthSnapshot.iArmour;

    /*CTask* activeTask = player->m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();

    if (activeTask && activeTask->GetTaskType() != eTaskType::TASK_COMPLEX_JUMP)
    {
        player->m_pPed->m_vecMoveSpeed = player->m_onFootSnapshotInterpolated.vecMoveSpeed;
    }*/

    CStatsSync::SaveLocalStats();
    plugin::CallMethod<0x60EA90, CPlayerPed*>(This);
    CStatsSync::RestoreLocalStats();

    // player->m_pPed->m_fAimingRotation = player->m_onFootSnapshotInterpolated.aimingRotation.m_angle;

    CWorld::PlayerInFocus = 0;

    CKeySync::ApplyLocalContext();
    CAimSync::ApplyLocalContext();
    // CStatsSync::ApplyLocalContext();
}

// CPlayerInfo::Process gives vehicle stunt money/stats (two wheels, wheelie, stoppie...) to the local
// player whenever he is inside a vehicle, also as a passenger of a network player; only do it for the driver
static DWORD CPlayerInfo__Process_VehicleStunts_Continue = 0x56F9BE;
static DWORD CPlayerInfo__Process_VehicleStunts_Skip = 0x5700D0;
static void __declspec(naked) CPlayerInfo__Process_VehicleStunts_Hook()
{
    __asm
    {
        mov edi, [eax + 0x58C] // ped->m_pVehicle
        test edi, edi
        jz skip
        cmp [edi + 0x460], eax // vehicle->m_pDriver == ped
        jne skip
        jmp CPlayerInfo__Process_VehicleStunts_Continue
    skip:
        jmp CPlayerInfo__Process_VehicleStunts_Skip
    }
}

static void CollectShotSnapshot(Packets::Players::PlayerBulletShot& packet, CEntity* owner, CVector* startPoint,
    CVector* endPoint, CColPoint* colPoint, CEntity* victim, int incrementalHit, eWeaponType weaponType)
{
    packet.iWeaponType = weaponType;
    packet.startPos = *startPoint;
    if (victim)
    {
        packet.endPos = colPoint->m_vecPoint;
    }
    else
    {
        packet.endPos = *endPoint;
    }
    if (!victim)
    {
        packet.bHitSomething = false;
        packet.bHitNetworkEntity = false;
    }
    else
    {
        packet.bHitSomething = true;
        switch (victim->m_nType)
        {
            case ENTITY_TYPE_VEHICLE:
            case ENTITY_TYPE_PED:
                packet.bHitNetworkEntity = true;
                break;
            default:
                packet.bHitNetworkEntity = false;
                break;
        }
    }
    if (packet.bHitNetworkEntity)
    {
        packet.hitEntity.SetEntity(victim);
        if (victim->m_nType == ENTITY_TYPE_PED)
        {
            packet.pedPiece = (ePedPieceTypes)colPoint->m_nSurfaceTypeB;
        }
    }

    packet.incrementalHit = incrementalHit;
}

static void __fastcall CWeapon__DoBulletImpact_Hook(CWeapon* weapon, SKIP_EDX, CEntity* owner, CEntity* victim,
    CVector* startPoint, CVector* endPoint, CColPoint* colPoint, int incrementalHit)
{
    if (owner == FindPlayerPed(0))
    {
        if ((!victim || (victim->m_nType != ENTITY_TYPE_PED && victim->m_nType != ENTITY_TYPE_VEHICLE)) && weapon->m_eWeaponType == WEAPON_MINIGUN)
        {
            weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
            return;
        }
        Packets::Players::PlayerBulletShot packet{};
        CollectShotSnapshot(packet, owner, startPoint, endPoint, colPoint, victim, incrementalHit, ((CPed*)owner)->GetWeapon().m_eWeaponType);
        GetPacketFactory().Send(packet);

        auto localPlayer = static_cast<CPed*>(owner);
        bool cancelForce = localPlayer->m_nPedFlags.bInVehicle && victim == localPlayer->m_pVehicle;
        if (CConfigLoader::ms_bRemoteBulletNoForce && victim && victim->m_nType == ENTITY_TYPE_VEHICLE &&
            CPools::ms_pVehiclePool)
        {
            auto vehicle = static_cast<CVehicle*>(victim);
            if (CPools::ms_pVehiclePool->IsObjectValid(vehicle) && vehicle->IsVTableValid())
            {
                cancelForce = cancelForce || CNetworkPlayerManager::GetPlayer(vehicle->m_pDriver) != nullptr;
                for (auto passenger : vehicle->m_apPassengers)
                    cancelForce = cancelForce || CNetworkPlayerManager::GetPlayer(passenger) != nullptr;
            }
        }
        CBulletImpactForceGuard forceGuard(cancelForce ? victim : nullptr, weapon->m_eWeaponType, false);
        weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
    }
    else if (owner && owner->m_nType == eEntityType::ENTITY_TYPE_PED)
    {
        CPed* ped = (CPed*)owner;

        if (!ped->IsPlayer())
        {
            weapon->DoBulletImpact(owner, victim, startPoint, endPoint, colPoint, incrementalHit);
        }
    }
}

static void __fastcall CPedIK__PointGunInDirection_Hook(
    CPedIK* This, SKIP_EDX, float dirX, float dirY, char flag, float float1)
{
    // if (This->m_pPed == FindPlayerPed(0))
    //{
    //     // save last aim data for syncing
    //     CLocalPlayer::m_vecLastAimX = dirX;
    //     CLocalPlayer::m_vecLastAimY = dirY;

    //    This->PointGunInDirection(dirX, dirY, flag, float1);
    //    return;
    //}

    // CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(This->m_pPed);

    // if (player == nullptr)
    //{
    //     This->PointGunInDirection(dirX, dirY, flag, float1);
    //     return;
    // }

    // if (player->m_playerOnFoot == nullptr)
    //     return;

    // player->m_pPed->m_fAimingRotation = player->m_playerOnFoot.aimingRotation;
    //
    // eWeaponType weapon = player->m_pPed->m_aWeapons[player->m_pPed->m_nActiveWeaponSlot].m_eWeaponType;

    ///*if (weapon != WEAPON_SNIPERRIFLE)
    //    dirY = player->m_aimSyncData.aimY;*/

    This->PointGunInDirection(dirX, dirY, flag, float1);
}

static void __fastcall CPlayerPed__dctor_Hook(CPlayerPed* This, SKIP_EDX)
{
    This->m_pPlayerData->m_nMeleeWeaponAnimReferencedExtra = 0;

    plugin::CallMethod<0x6093B0, CPlayerPed*>(This);
}

void CReferences__RemoveReferencesToPlayer_Hook()
{
    plugin::Call<0x571AD0>();  // CReferences::RemoveReferencesToPlayer();

    if (CNetwork::m_bAuthenticated)
    {
        // the game has not moved the respawned ped yet (C5): the packet is sent from ProcessControl
        g_bRespawnPending = true;
        g_nRespawnWaitFrames = 0;
    }
}

bool __fastcall CWeapon__TakePhotograph_Hook(CWeapon* This, SKIP_EDX, CEntity* entity, CVector* point)
{
    if (entity == FindPlayerPed(0))
    {
        return CMissionPhotos::TakePhotograph(This, entity, point);
    }

    return false;
}

void __fastcall CTaskSimpleJetPack__DropJetPack_Hook(CTaskSimpleJetPack* This, SKIP_EDX, CPed* ped)
{
    if (ped != FindPlayerPed(0) && ped->IsPlayer())
    {
        // dont create a pickup if the player is network
        This->m_bIsFinished = true;
    }

    plugin::CallMethod<0x67B660, CTaskSimpleJetPack*>(This, ped);  // CTaskSimpleJetPack::DropJetPack
}

void __fastcall CPedDamageResponseCalculator__ComputeWillKillPed_Hook(
    uintptr_t This, SKIP_EDX, CPed* ped, CPedDamageResponseInfo* dmgResponse, bool speak)
{
    plugin::CallMethod<0x4B3210, uintptr_t>(This, ped, dmgResponse, speak);

    if (ped == FindPlayerPed(0))
    {
        return;
    }

    // ped-damage-authority: what the local player did to a copy stays and is reported; what one of our NPCs did to
    // another player is reported before it is undone below
    if (CPedDamageSync::OnDamageComputed(reinterpret_cast<void*>(This), ped, dmgResponse))
    {
        return;
    }

    if (ped->m_nPedType > 3)  // peds
    {
        if (auto networkPed = CNetworkPedManager::GetPed(ped))
        {
            if (!networkPed->m_bSyncing)
            {
                if (networkPed->m_fHealth >= 1.0f)
                {
                    ped->m_fHealth = networkPed->m_fHealth;
                    dmgResponse->m_bHealthZero = false;
                    dmgResponse->m_bForceDeath = false;
                    dmgResponse->m_fDamageHealth = 0.0f;
                    dmgResponse->m_fDamageArmor = 0.0f;
                }
            }
        }

        return;
    }

    // players
    if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped))
    {
        if (networkPlayer->m_onFootSnapshotInterpolated.healthSnapshot.iHealth >= 1.0f)
        {
            ped->m_fHealth = networkPlayer->m_onFootSnapshotInterpolated.healthSnapshot.iHealth;
            dmgResponse->m_bHealthZero = false;
            dmgResponse->m_bForceDeath = false;
            dmgResponse->m_fDamageHealth = 0.0f;
            dmgResponse->m_fDamageArmor = 0.0f;
        }
    }
}

bool __fastcall CPlayerPed__CanPlayerStartMission_Hook(CPlayerPed* This, SKIP_EDX)
{
    return This->CanPlayerStartMission() /* && CLocalPlayer::m_bIsHost*/;
}

void __fastcall CPlayerPed__ProcessWeaponSwitch_Hook(CPlayerPed* This, SKIP_EDX, CPad* pad)
{
    if (CWorld::PlayerInFocus == 0)
    {
        This->ProcessWeaponSwitch(pad);
    }
}

void __fastcall CTaskSimplePlayerOnFoot__PlayIdleAnimations_Hook(
    CTaskSimplePlayerOnFoot* This, SKIP_EDX, CPlayerPed* playerPed)
{
    if (CWorld::PlayerInFocus == 0)
    {
        plugin::CallMethod<0x6872C0>(This, playerPed);
    }
}

bool __fastcall CPad__JumpJustDown_Hook(CPad* This)
{
    if (CWorld::PlayerInFocus == 0)
    {
        return This->JumpJustDown();
    }

    return false;
}

void __fastcall CTaskComplexJump_CTaskManager__SetTask_Hook(
    CTaskManager* This, SKIP_EDX, CTask* task, int tasksId, bool a4)
{
    This->SetTask(task, tasksId, a4);

    if (CWorld::PlayerInFocus == 0)
    {
        CLocalPlayer::BuildTaskPacket(TASK_COMPLEX_JUMP);
    }
}

void __fastcall CRunningScript__DoDeatharrestCheck_Hook(CRunningScript* This, SKIP_EDX)
{
    if (!This->m_bWastedBustedCheck)
    {
        return;
    }

    if (!CTheScripts::IsPlayerOnAMission())
    {
        return;
    }

    bool wastedOrBusted = false;

    auto* playerInfo = &CWorld::Players[CWorld::PlayerInFocus];

    if (playerInfo->m_nPlayerState == ePlayerState::PLAYERSTATE_HASDIED ||
        playerInfo->m_nPlayerState == ePlayerState::PLAYERSTATE_HASBEENARRESTED)
    {
        wastedOrBusted = true;
    }

    // the coop owner fails the mission of a guest that fell or of his own will (CCoopMission); only the mission script
    // takes it, the other scripts with the check leave it for it
    const bool bForced = This->m_bIsMission && CCoopMission::TakeForcedFail();
    if (bForced)
        wastedOrBusted = true;

    // Only the local player's own state fails a mission here (docs/REQUISITOS.md 1): an individual mission (story or side) is
    // lost by who plays it and by nobody else. This used to also fail a mission that is not synced when ANY other player was
    // dead or arrested, so two players doing their own Vigilante lost it together. In a synced mission (story or coop side
    // mission: the script runs EnableSyncingThisScript) the authority only fails for his own state too: a fallen guest drops
    // out from his own machine (CCoopMission) and the mission goes on here; if everyone falls the authority ends it anyway.

    if (wastedOrBusted)
    {
        // why this mission is failed, for the logs: his own state, or the coop owner failing it for everybody
        char szInfo[96];
        snprintf(szInfo, sizeof(szInfo), "mission-fail-check script=%.8s playerState=%d forced=%d focus=%d", This->m_szName,
            (int)playerInfo->m_nPlayerState, bForced ? 1 : 0, (int)CWorld::PlayerInFocus);
        CMissionMirror::LogEvent(szInfo);

        CMessages::ClearSmallMessagesOnly();
        memset(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag], 0, sizeof(uint32_t));

        if (This->m_nSP > 1u)
        {
            uint16_t nsp = This->m_nSP;
            do
            {
                --nsp;
            } while (nsp > 1u);
            This->m_nSP = nsp;
        }
        This->m_pCurrentIP = This->m_apStack[--This->m_nSP];

        This->m_bWastedOrBusted = true;
        This->m_nWakeTime = 0;
    }
}

void __fastcall CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer(
    CPedIntelligence* This, SKIP_EDX, uint16_t nLengthOfDuck)
{
    CPlayerPed* pPlayerPed = (CPlayerPed*)This->m_pPed;
    if (pPlayerPed == FindPlayerPed(0))
    {
        CLocalPlayer::BuildTaskPacket(eTaskType::TASK_SIMPLE_DUCK, true);
    }
    This->SetTaskDuckSecondary(nLengthOfDuck);
}

void __fastcall CPedIntelligence__ClearTaskDuckSecondary_HookTaskPlayer(CPedIntelligence* This, SKIP_EDX)
{
    CPlayerPed* pPlayerPed = (CPlayerPed*)This->m_pPed;
    if (pPlayerPed == FindPlayerPed(0))
    {
        CLocalPlayer::BuildTaskPacket(eTaskType::TASK_SIMPLE_DUCK, false);
    }
    This->ClearTaskDuckSecondary();
}

bool __fastcall CPad__DuckJustDown_Hook(CPad* pPad, SKIP_EDX)
{
    if (CWorld::PlayerInFocus != 0)
    {
        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(FindPlayerPed());
        if (pNetworkPlayer && pNetworkPlayer->m_bRequestedDuckTask)
        {
            pNetworkPlayer->m_bRequestedDuckTask = false;
            return true;
        }
        return false;
    }
    return pPad->DuckJustDown();
}

void CamShakeNoPos_Hook(CCamera* pCamera, float a2)
{
    if (CWorld::PlayerInFocus == 0)
    {
        plugin::Call<0x50A970>(pCamera, a2);
    }
}

static void __fastcall DisbandPlayerGroup_Hook(CPlayerPed* player, void*)
{
    player->DisbandPlayerGroup();
    if (!CConfigLoader::ms_bRecruitDismissSync || !CNetwork::m_bAuthenticated || player != FindPlayerPed(0))
        return;
    auto* group = CPedGroups::GetPedsGroup(player);
    if (!group || !group->m_groupMembership.IsLeader(player))
        return;

    // The vanilla disband ignores MISSION_CHAR, which is also used to keep remote copies streamed in.
    for (int slot = 0; slot < 7; ++slot)
    {
        auto* ped = group->m_groupMembership.GetMember(slot);
        auto* remote = ped ? CNetworkPedManager::GetPed(ped) : nullptr;
        if (remote && !remote->m_bSyncing && remote->m_nCreatedBy == RANDOM_CHAR &&
            ped->m_nPedType == PED_TYPE_GANG2)
            group->m_groupMembership.RemoveMember(slot);
    }
}

void PlayerHooks::InjectHooks()
{
    patch::RedirectCall(0x60D3D1, DisbandPlayerGroup_Hook);
    patch::RedirectCall(0x60D49B, DisbandPlayerGroup_Hook);
    patch::SetPointer(0x86D190, CPlayerPed__ProcessControl_Hook);
    patch::RedirectJump(0x56F9B0, CPlayerInfo__Process_VehicleStunts_Hook);

    patch::RedirectCall(0x73CD92, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x741199, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x7411DF, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x7412DF, CWeapon__DoBulletImpact_Hook);
    patch::RedirectCall(0x741E30, CWeapon__DoBulletImpact_Hook);

    // patch::RedirectCall(0x5FDF7A, CPedIK__PointGunInDirection_Hook);
    // patch::RedirectCall(0x61F351, CPedIK__PointGunInDirection_Hook);
    // patch::RedirectCall(0x62876D, CPedIK__PointGunInDirection_Hook);

    // fix CPlayerPed dctor crash
    patch::RedirectCall(0x60A9A3, CPlayerPed__dctor_Hook);

    // called when the player respawns after being busted or wasted
    patch::RedirectCall(0x443082, CReferences__RemoveReferencesToPlayer_Hook);

    patch::RedirectCall(0x74278B, CWeapon__TakePhotograph_Hook);

    patch::RedirectCall(0x5707AE, CTaskSimpleJetPack__DropJetPack_Hook);
    patch::RedirectCall(0x67E840, CTaskSimpleJetPack__DropJetPack_Hook);

    patch::RedirectCall(0x4B5B27, CPedDamageResponseCalculator__ComputeWillKillPed_Hook);

    patch::RedirectCall(0x4577E6, CPlayerPed__CanPlayerStartMission_Hook);
    patch::RedirectCall(0x4895B0, CPlayerPed__CanPlayerStartMission_Hook);

    patch::RedirectCall(
        0x60F2E0, CPlayerPed__ProcessWeaponSwitch_Hook);  // disable switching weapon for network players

    patch::RedirectCall(0x6887E2, CTaskSimplePlayerOnFoot__PlayIdleAnimations_Hook);

    patch::RedirectCall(0x688700, CPad__JumpJustDown_Hook);
    patch::RedirectCall(0x6887D8, CTaskComplexJump_CTaskManager__SetTask_Hook);

    patch::ReplaceFunction(0x485A50, CRunningScript__DoDeatharrestCheck_Hook);

    //////////////////// player ducking sync ////////////////////
    patch::RedirectCall(0x6879B1, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);
    patch::RedirectCall(0x687F1A, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);
    patch::RedirectCall(0x6886B2, CPedIntelligence__SetTaskDuckSecondary_HookTaskPlayer);

    patch::RedirectCall(0x688290, CPedIntelligence__ClearTaskDuckSecondary_HookTaskPlayer);

    // dont let the keysync control ducking for network players
    patch::RedirectCall(0x68798B, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x687EFC, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x687FFA, CPad__DuckJustDown_Hook);
    patch::RedirectCall(0x688689, CPad__DuckJustDown_Hook);
    //////////////////////////////////////////////////////////////////

    // disable cam shake when a network player shoots the sniper rifle
    patch::RedirectCall(0x73ACE2, CamShakeNoPos_Hook);
}
