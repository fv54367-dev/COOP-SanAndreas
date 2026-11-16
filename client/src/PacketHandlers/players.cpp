#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CPedDamageSync.h"
#include "CBulletImpactForceGuard.h"
#include <CAimSync.h>
#include <CEntryExitTransitionSync.h>
#include <CProjectileInfo.h>
#include "CTestMode.h"
#include "CMissionMirror.h"
#include "CJackSync.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "UI/CChat.h"
#include <cmath>
#include <cstring>

PACKET_HANDLER(ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pOnFootUpdate->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;

    pNetworkPlayer->NoteNetState(pOnFootUpdate->healthSnapshot.iHealth, pOnFootUpdate->healthSnapshot.iArmour,
        &pOnFootUpdate->weaponSnapshot, &pOnFootUpdate->vecPos);

    if (pNetworkPlayer->m_bDead)
    {
        // still dead (or arrested): keep the pose, a live update would stand the corpse up again. Only a respawn
        // packet ends the state, the timeout is a last resort for a lost packet or a scripted resurrection
        if (GetTickCount() - pNetworkPlayer->m_deadSince < 20000)
        {
            return;
        }

        pNetworkPlayer->m_bDead = false;
        if (pPlayerPed != nullptr)
        {
            pNetworkPlayer->DestroyPed();
            pPlayerPed = nullptr;
        }
    }

    if (pPlayerPed == nullptr)
    {
        pNetworkPlayer->CreatePed(pOnFootUpdate->vecPos);
        pPlayerPed = pNetworkPlayer->m_pPed;

        if (pPlayerPed == nullptr)
        {
            return;  // the player pool is full, try again with the next update
        }
    }

    if (CJackSync::IsPlayerJackInProgress(pPlayerPed))
    {
        // Keep health current, but the native task owns position, velocity, rotation and pose.
        pNetworkPlayer->m_onFootSnapshotInterpolated = *pOnFootUpdate;
        pPlayerPed->m_fHealth = pOnFootUpdate->healthSnapshot.iHealth;
        pPlayerPed->m_fArmour = pOnFootUpdate->healthSnapshot.iArmour;
        pNetworkPlayer->m_bHasPosError = false;
        return;
    }

    if (pPlayerPed->m_pVehicle != nullptr && pPlayerPed->m_nPedFlags.bInVehicle)
    {
        pNetworkPlayer->RemoveFromVehicle(pPlayerPed->m_pVehicle);
    }

    if (CConfigLoader::ms_bSmoothOnFoot && !pOnFootUpdate->bHasJetpack)
    {
        pPlayerPed->SetPosn(NetworkSmoothing::ApplyTarget(pPlayerPed->GetPosition(), pOnFootUpdate->vecPos,
            pNetworkPlayer->m_vecPosError, pNetworkPlayer->m_bHasPosError, 3.0f));
    }
    else
    {
        pPlayerPed->SetPosn(pOnFootUpdate->vecPos);
        pNetworkPlayer->m_bHasPosError = false;
    }
    pNetworkPlayer->m_pPed->m_vecMoveSpeed = pOnFootUpdate->vecMoveSpeed;

    pNetworkPlayer->ApplyWeaponSnapshot(pOnFootUpdate->weaponSnapshot);

    pNetworkPlayer->m_pPed->m_fCurrentRotation = pOnFootUpdate->currentRotation.m_angle;
    pNetworkPlayer->m_pPed->m_fAimingRotation = pOnFootUpdate->aimingRotation.m_angle;
    // the interior ownership needs to know what players are already inside (the enex packet also carries it)
    pNetworkPlayer->m_pPed->m_nAreaCode = pOnFootUpdate->areaCode;

    CUtil::SetPlayerJetpack(pNetworkPlayer, pOnFootUpdate->bHasJetpack);

    if (CUtil::IsDucked(pNetworkPlayer->m_pPed) != pOnFootUpdate->bDucking)
    {
        if (pOnFootUpdate->bDucking)
        {
            pNetworkPlayer->m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);
            // abort fighting and jumping tasks maybe?
        }
        else
        {
            pNetworkPlayer->m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
        }
    }

    pNetworkPlayer->m_pPed->m_nFightingStyle = pOnFootUpdate->iFightingStyle;
    pNetworkPlayer->m_pPed->m_nAllowedAttackMoves |= 15u;

    pNetworkPlayer->SetKeys(pOnFootUpdate->keySnapshot);

    pNetworkPlayer->m_onFootSnapshotInterpolated = *pOnFootUpdate;
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pKeyPressed->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    if (pNetworkPlayer->m_pPed == nullptr)
        return;

    if (CJackSync::IsPlayerJackInProgress(pNetworkPlayer->m_pPed))
        return;

    pNetworkPlayer->SetKeys(pKeyPressed->keySnapshot);

    pNetworkPlayer->m_pPed->m_fCurrentRotation = pKeyPressed->currentRotation.m_angle;
    pNetworkPlayer->m_pPed->m_fAimingRotation = pKeyPressed->aimingRotation.m_angle;

    pNetworkPlayer->m_onFootSnapshotInterpolated.currentRotation = pKeyPressed->currentRotation;
    pNetworkPlayer->m_onFootSnapshotInterpolated.aimingRotation = pKeyPressed->aimingRotation;
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerCameraSync->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_startedInterpolatingCameraAt = GetTickCount();
    pNetworkPlayer->m_cameraSnapshotOld = pNetworkPlayer->m_cameraSnapshot;
    pNetworkPlayer->m_cameraSnapshot = *pPlayerCameraSync;
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK, Packets::Players::SetPlayerTask* pSetPlayerTask)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pSetPlayerTask->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->HandleTask(*pSetPlayerTask);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition)
{
    CEntryExitTransitionSync::Receive(*pEnExTransition);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerPlaceWaypoint->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("WAYPOINT PLACE %d %.0f %.0f\n", packet->place, packet->position.x, packet->position.y);
#endif
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRespawnPlayer->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->Respawn(*pRespawnPlayer);
}

PACKET_HANDLER(ePacketType::PLAYER_DEATH, Packets::Players::PlayerDeath* pPlayerDeath)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerDeath->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->HandleDeath(*pPlayerDeath);

    // a participant of a synced story mission that dies or is arrested fails it for everybody (docs/REQUISITOS.md 1): the
    // script authority (the host) fails its script through the death path. Coop side missions do it through CCoopMission,
    // individual players are not part of the mission
    if (CCoopMission::IsScriptAuthority() && !CCoopMission::IsRunning() && COpCodeSync::IsSyncedMissionRunning() &&
        CCoopMission::ShouldIncludeNetworkPlayer(pNetworkPlayer))
    {
        const bool bBusted = pPlayerDeath->deathState == Packets::Players::PlayerDeath::BUSTED;
        CMissionMirror::LogEventf("guest-fell player=%d busted=%d", (int)pNetworkPlayer->m_playerId.value(), bBusted ? 1 : 0);
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + pNetworkPlayer->GetName() + (bBusted ? " fue arrestado" : " murio") +
                          ": la mision fallo para todos.");
        CCoopMission::RequestMissionFail();
    }
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerBulletShot->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    if (pNetworkPlayer->m_pPed == nullptr)
        return;

    pNetworkPlayer->m_pPed->SetCurrentWeapon(pPlayerBulletShot->iWeaponType);

    CEntity* owner = pNetworkPlayer->m_pPed;
    CEntity* victim = nullptr;
    CVector startPoint = pPlayerBulletShot->startPos;
    CVector endPoint = pPlayerBulletShot->endPos;
    const auto lengthSquared = [](const CVector& v) { return v.x * v.x + v.y * v.y + v.z * v.z; };
    CColPoint colPoint;
    std::memset(&colPoint, 0, sizeof(colPoint));
    int incrementalHit = pPlayerBulletShot->incrementalHit;

    if (pPlayerBulletShot->bHitSomething)
    {
        CVector endPosCalced = (endPoint - startPoint);
        endPosCalced.Normalise();
        endPosCalced = endPoint + endPosCalced * 10.0f;
        endPoint = endPosCalced;
        const bool bLocalHit = CWorld::ProcessLineOfSight(startPoint, endPoint, colPoint, victim, 1, 1, 1, 1, 1, 0, 0, 1);
        if (!bLocalHit)
        {
            std::memset(&colPoint, 0, sizeof(colPoint));
        }

        const float normalLengthSquared = lengthSquared(colPoint.m_vecNormal);
        if (!bLocalHit || !std::isfinite(normalLengthSquared) ||
            normalLengthSquared < 0.81f || normalLengthSquared > 1.21f)
        {
            CVector fallbackNormal = startPoint - pPlayerBulletShot->endPos;
            const float shotLengthSquared = lengthSquared(fallbackNormal);
            if (std::isfinite(shotLengthSquared) && shotLengthSquared > 0.000001f)
            {
                fallbackNormal *= 1.0f / std::sqrt(shotLengthSquared);
            }
            else
            {
                fallbackNormal.Set(0.0f, 0.0f, 1.0f);
            }
            colPoint.m_vecNormal = fallbackNormal;
        }
        colPoint.m_vecPoint = pPlayerBulletShot->endPos;

        if (pPlayerBulletShot->bHitNetworkEntity)
        {
            victim = pPlayerBulletShot->hitEntity.GetEntity();
            if (pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PLAYER ||
                pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PED)
            {
                colPoint.m_nSurfaceTypeB = pPlayerBulletShot->pedPiece;
            }
        }
    }
    else
    {
        colPoint.m_vecPoint.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_vecNormal.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_nLightingB.day = 0;
        colPoint.m_nLightingB.night = 0;
        colPoint.m_nPieceTypeB = 0;
        colPoint.m_nSurfaceTypeB = 0;
    }

    if (victim && !victim->IsVTableValid())
    {
        victim = nullptr;
    }

    // DoBulletImpact uses this offset as the vehicle force lever arm.
    if (victim && victim->m_nType == ENTITY_TYPE_VEHICLE &&
        lengthSquared(pPlayerBulletShot->endPos - victim->GetPosition()) > 15.0f * 15.0f)
    {
        colPoint.m_vecPoint = victim->GetPosition();
    }

    // the game adds the hit (weapon skill, bullets that hit...) to the stats of the shooter when it is a player: without this the
    // other player's aim trained everybody's own stats. What it adds belongs to the other player and is dropped here.
    CStatsSync::SaveLocalStats();
    {
        CBulletImpactForceGuard forceGuard(victim, pNetworkPlayer->m_pPed->GetWeapon().m_eWeaponType, true);
        // ped-damage-authority: the shooter reports what he did to an NPC (PED_DAMAGE): the replay must not hurt it again
        CPedDamageSync::ms_bReplayingRemoteShot = true;
        pNetworkPlayer->m_pPed->GetWeapon().DoBulletImpact(
            owner, victim, &startPoint, &pPlayerBulletShot->endPos, &colPoint, incrementalHit);
        CPedDamageSync::ms_bReplayingRemoteShot = false;
        CPedDamageSync::AddReplayBlood(victim, colPoint, incrementalHit);
    }
    CStatsSync::RestoreLocalStats();
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile)
{
    CEntity* creator = pAddProjectile->creator.GetEntity();
    if (creator == nullptr)
    {
        return;
    }

    CVector origin = pAddProjectile->origin;

    // the camera context is only needed (and only safe) while the remote player actually has a ped: it can be
    // gone between DestroyPed and CreatePed during a respawn, or if CreatePed failed
    bool aimContextApplied = false;
    if (pAddProjectile->creator.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(creator))
        {
            if (networkPlayer->m_pPed)
            {
                CAimSync::ApplyNetworkPlayerContext(networkPlayer);
                aimContextApplied = true;
            }
        }
    }

    CEntity* pTarget = nullptr;
    if (pAddProjectile->bTarget)
    {
        pTarget = pAddProjectile->target.GetEntity();
        if (pTarget && !pTarget->IsVTableValid())
        {
            pTarget = nullptr;
        }
    }

    // bDir = the sender had a direction (thrown/launched); without it the game picks the default one
    // same as the bullets: a projectile of another player must not add to the local stats
    CStatsSync::SaveLocalStats();
    CProjectileInfo::AddProjectile(creator, pAddProjectile->projectileType, origin, pAddProjectile->force,
        pAddProjectile->bDir ? &pAddProjectile->dir : nullptr, pTarget);
    CStatsSync::RestoreLocalStats();

    if (aimContextApplied)
    {
        CAimSync::ApplyLocalContext();
    }
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerStats->senderPlayerid))
    {
        for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
        {
            eStats statId = CStatsSync::m_aeSyncedStats[i];
            pNetworkPlayer->m_stats[statId] = pPlayerStats->stats[i];
        }

        if (pNetworkPlayer->m_pPed)
            pNetworkPlayer->m_pPed->m_fMaxHealth = std::max(100.0f, pNetworkPlayer->m_stats[STAT_MAX_HEALTH] / 5.69f);
    }
}

PACKET_HANDLER(ePacketType::TEST_COMMAND, Packets::Players::TestCommand* pTestCommand)
{
    CTestMode::HandleTestCommand(pTestCommand);
}

PACKET_HANDLER(ePacketType::GIVE_AMMO, Packets::Players::GiveAmmo* pGiveAmmo)
{
    CChatCommands::HandleGiveAmmo(pGiveAmmo);
}

PACKET_HANDLER(ePacketType::PLAYER_PERSIST_STATE, Packets::Players::PlayerPersistState* pPersistState)
{
    CPersistenceSync::HandleState(pPersistState);
}

PACKET_HANDLER(ePacketType::PERSIST_SAVE_REQUEST, Packets::Players::PersistSaveRequest* pPersistSaveRequest)
{
    CPersistenceSync::RequestImmediateSend();
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRebuildPlayer->senderPlayerid))
    {
        pNetworkPlayer->m_stats[STAT_FAT] = pRebuildPlayer->clothesDesc.m_fFatStat;
        pNetworkPlayer->m_stats[STAT_MUSCLE] = pRebuildPlayer->clothesDesc.m_fMuscleStat;

        //CStatsSync::ApplyNetworkPlayerContext(networkPlayer);

        pNetworkPlayer->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;

        if (auto pPlayerPed = pNetworkPlayer->m_pPed)
        {
            *pPlayerPed->m_pPlayerData->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;
            if (pPlayerPed->m_pRwClump)
            {
                CMissionMirror::LogEventf("net-rebuild-packet cutsceneRunning=%d loadStatus=%d", CCutsceneMgr::ms_running ? 1 : 0, (int)CCutsceneMgr::ms_cutsceneLoadStatus);
                CClothes::RebuildPlayer(pPlayerPed, false);
            }
        }

        //CStatsSync::ApplyLocalContext();
    }
}

#include "CGpsRoute.h"
PACKET_HANDLER(ePacketType::VEHICLE_GPS, Packets::Players::VehicleGps* packet)
{
    CGpsRoute::OnPacket(*packet);
}
