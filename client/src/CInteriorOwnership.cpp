#include "stdafx.h"
#include "CInteriorOwnership.h"
#include "CInteriorObjectSync.h"
#include <COpCodeSync.h>
#include <CCoopMission.h>
#include <CMissionMode.h>
#include <CMissionMirror.h>

CInteriorOwnership::Role CInteriorOwnership::s_role = CInteriorOwnership::Role::None;
playerid_t CInteriorOwnership::s_ownerId;
unsigned char CInteriorOwnership::s_nAreaCode = 0;
bool CInteriorOwnership::s_bOwnerUnconfirmed = false;
unsigned int CInteriorOwnership::s_nOwnerMissingTicks = 0;
uint32_t CInteriorOwnership::s_nHandoverGraceUntil = 0;
bool CInteriorOwnership::s_bGotTakeover = false;
std::list<CPed*> CInteriorOwnership::s_hiddenPeds;

namespace
{
    std::list<CPed*> servicePeds;
    uint32_t lastPopulationRequest = 0;
    CVector populationAnchor{};
}

bool CInteriorOwnership::IsServicePed(CPed* ped)
{
    return ped && std::find(servicePeds.begin(), servicePeds.end(), ped) != servicePeds.end();
}

void CInteriorOwnership::ObserveLocalPed(CPed* ped)
{
    if (!CConfigLoader::ms_bInteriorPopulationSync || !ped || ped->IsPlayer() ||
        COpCodeSync::bProcessingNetworkOpcode || IsServicePed(ped))
        return;
    servicePeds.remove(nullptr);
    auto script = COpCodeSync::GetScriptRunningCommand();
    if (!script || script->m_bIsMission)
        return;
    // Classify by the creating service script, never by model or global mission state.
    for (const char* name : {"BARB", "JFUD", "CLOTH", "AMUNAT", "TATTO", "PSHOP", "BAR"})
    {
        if (_strnicmp(script->m_szName, name, 8) != 0)
            continue;
        servicePeds.push_back(ped);
        ped->RegisterReference((CEntity**)&servicePeds.back());
        CMissionMirror::LogEventf("interior-service-create script=%.8s model=%d", script->m_szName, ped->m_nModelIndex);
        break;
    }
}

std::vector<CPed*> CInteriorOwnership::GetHiddenServicePeds()
{
    std::vector<CPed*> out;
    for (CPed* ped : s_hiddenPeds)
        if (ped && ped->IsVTableValid() && IsServicePed(ped))
            out.push_back(ped);
    return out;
}

bool CInteriorOwnership::AdoptServicePed(CNetworkPed* networkPed)
{
    if (!CConfigLoader::ms_bInteriorPopulationSync || !networkPed->m_bInteriorPopulation ||
        networkPed->m_nAreaCode != s_nAreaCode || s_nAreaCode == 0 || networkPed->m_fHealth <= 0.0f)
        return false;
    for (auto it = s_hiddenPeds.begin(); it != s_hiddenPeds.end(); ++it)
    {
        CPed* local = *it;
        if (!IsServicePed(local) || !local->IsVTableValid() || !local->m_matrix || !local->IsAlive() ||
            local->m_nModelIndex != networkPed->m_nModelId || CNetworkPedManager::GetPed(local) ||
            DistanceBetweenPoints(local->GetPosition(), networkPed->m_vecLastPos) > 4.0f)
            continue;
        // Keep the native service script's handle and tasks under the existing network identity.
        networkPed->DestroyGamePed("interior-adopt-service");
        networkPed->m_pPed = local;
        networkPed->m_bAdoptedServicePed = true;
        local->RegisterReference((CEntity**)&networkPed->m_pPed);
        local->CleanUpOldReference((CEntity**)&*it);
        s_hiddenPeds.erase(it);
        local->m_bIsVisible = true;
        local->m_bUsesCollision = true;
        local->m_fHealth = networkPed->m_fHealth;
        networkPed->m_nLocalModelId = local->m_nModelIndex;
        networkPed->m_bStreamedOut = false;
        networkPed->m_bPendingTaskState = false;
        networkPed->m_bHasLastTaskState = false;
        OnPedTakeover(s_nAreaCode);
        CMissionMirror::LogEventf("interior-adopt-service ped=%s model=%d area=%u",
            to_string(networkPed->m_pedId).c_str(), local->m_nModelIndex, s_nAreaCode);
        return true;
    }
    // Owner flicker on entry (testeo 18-11): we became owner again and RestoreHiddenPeds re-showed AND
    // re-hosted our native keepers before the take-over records arrived; the hidden pass above finds nothing
    // and the take-over would create a second, overlapping keeper (two pizzeros at the same till). Adopt the
    // visible native keeper into the take-over record and unpublish our own duplicate record of it.
    // NEAREST match: two same-model keepers stand 3.3 m apart, inside the 4 m radius (first-match would merge
    // the wrong keeper into the second take-over record).
    CPed* best = nullptr;
    float bestDist = 4.0f;
    for (CPed* local : servicePeds)
    {
        if (!local || !local->IsVTableValid() || !local->m_matrix || !local->IsAlive() ||
            local->m_nModelIndex != networkPed->m_nModelId)
            continue;
        const float dist = DistanceBetweenPoints(local->GetPosition(), networkPed->m_vecLastPos);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = local;
        }
    }
    if (best)
    {
        CNetworkPed* existing = CNetworkPedManager::GetPed(best);
        if (existing == networkPed)
            return true;  // already adopted
        if (existing && !existing->m_bSyncing)
            return false;  // somebody else's record: not ours to merge
        if (existing)
        {
            // our own duplicate publication of the same physical keeper: remove it for everyone, keep the ped
            CMissionMirror::LogEventf("interior-dedupe-service ped=%s keep=%s model=%d",
                to_string(existing->m_pedId).c_str(), to_string(networkPed->m_pedId).c_str(),
                best->m_nModelIndex);
            CNetworkPedManager::Remove(existing);
            delete existing;
        }
        networkPed->DestroyGamePed("interior-adopt-service");
        networkPed->m_pPed = best;
        networkPed->m_bAdoptedServicePed = true;
        best->RegisterReference((CEntity**)&networkPed->m_pPed);
        best->m_bIsVisible = true;
        best->m_bUsesCollision = true;
        best->m_fHealth = networkPed->m_fHealth;
        networkPed->m_nLocalModelId = best->m_nModelIndex;
        networkPed->m_bStreamedOut = false;
        networkPed->m_bPendingTaskState = false;
        networkPed->m_bHasLastTaskState = false;
        OnPedTakeover(s_nAreaCode);
        CMissionMirror::LogEventf("interior-adopt-service-visible ped=%s model=%d area=%u",
            to_string(networkPed->m_pedId).c_str(), best->m_nModelIndex, s_nAreaCode);
        return true;
    }
    return false;
}

void CInteriorOwnership::MaintainHiddenPeds()
{
    if (!CConfigLoader::ms_bInteriorPopulationSync) return;
    RefreshServicePeds();
    for (auto ped : s_hiddenPeds)
        if (ped && ped->IsVTableValid())
        {
            ped->m_bIsVisible = false;
            ped->m_bUsesCollision = false;
        }
}

void CInteriorOwnership::HandlePopulation(const Packets::Peds::InteriorPopulation& packet)
{
    auto local = FindPlayerPed(0);
    if (!CConfigLoader::ms_bInteriorPopulationSync || !local || packet.area == 0 ||
        packet.area != local->m_nAreaCode || packet.area != s_nAreaCode ||
        DistanceBetweenPoints(local->GetPosition(), packet.pos) >= SAME_INTERIOR_RADIUS || !packet.owner.valid())
        return;
    if (packet.owner == s_ownerId)
        return;
    CMissionMirror::LogEventf("interior-owner area=%u owner=%u local=%u", packet.area,
        packet.owner.value(), CNetworkPlayerManager::m_localPlayerId.value());
    if (packet.owner == CNetworkPlayerManager::m_localPlayerId)
        BecomeOwner(false);
    else
        BecomeGuest(packet.owner);
}

static bool IsValidPed(CPed* ped)
{
    return ped && IsPedPointerValid(ped) && ped->IsVTableValid() && ped->m_matrix;
}

// A vanilla guest has no local mission script; use the authority and participation state.
static bool IsAuthorityMissionPed(const CNetworkPed* networkPed)
{
    if (!CConfigLoader::ms_bMissionInteriorSafety || !CNetwork::m_bConnected || !networkPed ||
        networkPed->m_nCreatedBy != MISSION_CHAR || networkPed->m_bSyncing ||
        CCoopMission::IsScriptAuthority() || CMissionMode::IsIndividual() || CCoopMission::HasLocalLeft() ||
        COpCodeSync::IsLocalMissionRunning() ||
        (CCoopMission::IsActive() && !CCoopMission::IsLocalParticipant()) ||
        !CTheScripts::OnAMissionFlag || !CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
        return false;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || localPlayer->m_fHealth <= 0.0f ||
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASDIED ||
        CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED)
        return false;

    for (auto player : CNetworkPlayerManager::m_pPlayers)
    {
        const bool authority = CCoopMission::IsRunning() ? player->m_playerId == CCoopMission::GetOwnerId()
                                                       : player->m_bIsHost;
        if (authority && player->m_pPed && player->m_nRunningMission >= 0 &&
            CCoopMission::ShouldIncludeNetworkPlayer(player) &&
            networkPed->m_pedId.creator() == player->m_playerId)
            return true;
    }
    return false;
}

// mission peds of a coop mission are shared, and the ones of an individual mission are local to that player
static bool IsMissionPed(CPed* ped)
{
    // CREATE_CHAR marks MISSION_CHAR after CWorld::Add has already run.
    if (CConfigLoader::ms_bInteriorPopulationSync)
        if (auto script = COpCodeSync::GetScriptRunningCommand(); script && script->m_bIsMission)
            return true;
    if (CConfigLoader::ms_bInteriorPopulationSync && CInteriorOwnership::IsServicePed(ped))
        return false;
    if (ped->m_nCreatedBy != MISSION_CHAR)
        return false;

    return COpCodeSync::IsSyncedMissionRunning() || COpCodeSync::IsLocalOnlyMissionRunning() ||
           IsAuthorityMissionPed(CNetworkPedManager::GetPed(ped));
}

static bool IsInLocalInterior(CPed* ped, const CPlayerPed* localPlayer)
{
    if (!localPlayer || localPlayer->m_nAreaCode == 0)
        return false;

    if (ped->m_nAreaCode != 0 && ped->m_nAreaCode != localPlayer->m_nAreaCode)
        return false;
    if (!CConfigLoader::ms_bInteriorPopulationSync && ped->m_nAreaCode != 0)
        return true;

    return DistanceBetweenPoints(ped->GetPosition(), localPlayer->GetPosition()) < CInteriorOwnership::SAME_INTERIOR_RADIUS;
}

// a network player (not us) inside the given interior
static bool IsPlayerInInterior(const CNetworkPlayer* networkPlayer, const CPlayerPed* localPlayer, unsigned char areaCode)
{
    CPlayerPed* ped = networkPlayer ? networkPlayer->m_pPed : nullptr;
    if (!ped || ped == localPlayer || !ped->m_matrix)
        return false;

    if (DistanceBetweenPoints(ped->GetPosition(), localPlayer->GetPosition()) >= CInteriorOwnership::SAME_INTERIOR_RADIUS)
        return false;

    // a remote copy whose area is still unknown is taken as inside when it is this close (the main map is far away)
    return ped->m_nAreaCode != 0 ? ped->m_nAreaCode == areaCode : true;
}

static playerid_t FindLowestOtherPlayer(const CPlayerPed* localPlayer, unsigned char areaCode)
{
    playerid_t lowest;

    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!IsPlayerInInterior(networkPlayer, localPlayer, areaCode))
            continue;

        if (!lowest.valid() || networkPlayer->m_playerId < lowest)
            lowest = networkPlayer->m_playerId;
    }

    return lowest;
}

// used when the owner leaves: the lowest player id among the ones still inside becomes the owner
static uint32_t FindLowestPlayerIncludingLocal(const CPlayerPed* localPlayer, unsigned char areaCode)
{
    uint32_t lowest = CNetworkPlayerManager::m_localPlayerId.valid() ? CNetworkPlayerManager::m_localPlayerId.value()
                                                                   : 0xFFFFFFFFu;

    for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (IsPlayerInInterior(networkPlayer, localPlayer, areaCode) && networkPlayer->m_playerId.value() < lowest)
            lowest = networkPlayer->m_playerId.value();
    }

    return lowest;
}

static bool IsPlayerInInterior(playerid_t playerid, const CPlayerPed* localPlayer, unsigned char areaCode)
{
    return IsPlayerInInterior(CNetworkPlayerManager::GetPlayer(playerid), localPlayer, areaCode);
}

void CInteriorOwnership::RestoreHiddenPeds(bool alsoSync)
{
    for (auto it = s_hiddenPeds.begin(); it != s_hiddenPeds.end();)
    {
        CPed* ped = *it;
        if (CConfigLoader::ms_bInteriorPopulationSync && alsoSync && IsValidPed(ped))
        {
            bool represented = false;
            for (auto record : CNetworkPedManager::m_pPeds)
            {
                if (record->m_pPed == ped || record->m_nModelId != ped->m_nModelIndex ||
                    (!record->m_bInteriorPopulation && record->m_nCreatedBy == MISSION_CHAR) ||
                    (record->m_nAreaCode && record->m_nAreaCode != s_nAreaCode))
                    continue;
                const CVector pos = IsValidPed(record->m_pPed) ? record->m_pPed->GetPosition() : record->m_vecLastPos;
                if (DistanceBetweenPoints(pos, ped->GetPosition()) < 4.0f)
                {
                    represented = true;
                    break;
                }
            }
            if (represented)
            {
                ++it;
                continue;
            }
        }
        if (ped)
        {
            if (ped->IsVTableValid())
            {
                ped->m_bIsVisible = true;
                ped->m_bUsesCollision = true;

                if (alsoSync && ped->m_matrix && s_nAreaCode != 0 && ped->m_nPedType > 1 && !IsMissionPed(ped) &&
                    !CNetworkPedManager::GetPed(ped))
                {
                    CNetworkPed::CreateHosted(ped);
                }
            }

            ped->CleanUpOldReference((CEntity**)&*it);
        }
        it = s_hiddenPeds.erase(it);
    }
}

void CInteriorOwnership::HideLocalInteriorPeds()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    for (CPed* ped : CPools::ms_pPedPool)
    {
        if (ShouldHideLocalPed(ped))
            HideLocalPed(ped);
    }
}

void CInteriorOwnership::BecomeOwner(bool unconfirmed)
{
    s_role = Role::Owner;
    s_ownerId = CNetworkPlayerManager::m_localPlayerId;
    CInteriorObjectSync::OnRoleChanged();
    s_bOwnerUnconfirmed = unconfirmed;
    s_nOwnerMissingTicks = 0;
    const bool gotTakeover = CConfigLoader::ms_bInteriorPopulationSync && s_bGotTakeover;
    s_bGotTakeover = false;

    // D interior-handover: when the previous owner leaves, his peds are released (B2) and the take-overs arrive a
    // couple of ticks later; showing (and hosting) our own local population right away would flash a different one.
    // A take-over that already arrived while we were still his guest needs no extra wait.
    if (!unconfirmed && CConfigLoader::ms_bInteriorHandover)
    {
        s_bGotTakeover = gotTakeover;
        if (!s_bGotTakeover || CConfigLoader::ms_bInteriorPopulationSync)
            s_nHandoverGraceUntil = CConfigLoader::GetSyncTime() + INTERIOR_HANDOVER_GRACE;
        return;
    }

    RestoreHiddenPeds(s_nAreaCode != 0);
}

// D interior-handover: a ped of our interior was handed over to us; while the grace period runs, this keeps our own
// hidden population hidden (LeaveInterior shows it again when we finally leave)
void CInteriorOwnership::OnPedTakeover(unsigned char areaCode)
{
    if (s_nAreaCode == 0)
        return;

    // an unknown area (the record never learned it) is accepted: interiors are far away from the main map
    if (areaCode != 0 && areaCode != s_nAreaCode)
        return;

    s_bGotTakeover = true;
}

void CInteriorOwnership::BecomeGuest(playerid_t ownerId)
{
    bool wasGuest = s_role == Role::Guest;

    s_role = Role::Guest;
    s_ownerId = ownerId;
    CInteriorObjectSync::OnRoleChanged();
    s_bOwnerUnconfirmed = false;
    s_nOwnerMissingTicks = 0;
    s_nHandoverGraceUntil = 0;
    s_bGotTakeover = false;

    if (!wasGuest)
        HideLocalInteriorPeds();
}

void CInteriorOwnership::LeaveInterior()
{
    RestoreHiddenPeds(false);

    s_role = Role::None;
    s_ownerId = playerid_t();
    CInteriorObjectSync::OnRoleChanged();
    s_bOwnerUnconfirmed = false;
    s_nOwnerMissingTicks = 0;
    s_nHandoverGraceUntil = 0;
    s_bGotTakeover = false;
}

void CInteriorOwnership::Reset()
{
    LeaveInterior();
    s_nAreaCode = 0;
    lastPopulationRequest = 0;
    for (auto& ped : servicePeds)
        if (ped) ped->CleanUpOldReference((CEntity**)&ped);
    servicePeds.clear();
}

void CInteriorOwnership::Process()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return;

    unsigned char areaCode = localPlayer->m_nAreaCode;

    if (CConfigLoader::ms_bInteriorPopulationSync && CNetwork::m_bConnected)
    {
        const uint32_t now = CConfigLoader::GetSyncTime();
        if (localPlayer->m_fHealth <= 0.0f || CWorld::Players[0].m_nPlayerState == PLAYERSTATE_HASBEENARRESTED)
            areaCode = 0;
        const bool moved = s_nAreaCode != 0 && areaCode != 0 &&
            DistanceBetweenPoints(localPlayer->GetPosition(), populationAnchor) >= SAME_INTERIOR_RADIUS;
        if (areaCode != s_nAreaCode || moved)
        {
            LeaveInterior();
            s_nAreaCode = areaCode;
            populationAnchor = localPlayer->GetPosition();
            if (areaCode != 0)
                BecomeGuest(playerid_t{}); // wait for the server before publishing any local population
            lastPopulationRequest = 0;
        }
        if (lastPopulationRequest == 0 || now - lastPopulationRequest >= 500)
        {
            Packets::Peds::InteriorPopulation request{};
            request.area = areaCode;
            request.pos = localPlayer->GetPosition();
            GetPacketFactory().Send(request);
            lastPopulationRequest = now;
        }
        if (s_role == Role::Guest)
            HideLocalInteriorPeds(); // scripts may change visibility after CWorld::Add
        if (s_role == Role::Owner && s_nHandoverGraceUntil &&
            (int32_t)(now - s_nHandoverGraceUntil) >= 0)
        {
            s_nHandoverGraceUntil = 0;
            RestoreHiddenPeds(true); // reconcile each shadow; a partial takeover must not hide missing clerks
        }
        return;
    }

    if (areaCode == 0)
    {
        if (s_role != Role::None || s_nAreaCode != 0)
            LeaveInterior();
        s_nAreaCode = 0;
        return;
    }

    // changed interior without going through the main map: the hidden peds belong to the previous one
    if (s_nAreaCode != 0 && s_nAreaCode != areaCode)
        LeaveInterior();

    bool entered = s_nAreaCode != areaCode;
    s_nAreaCode = areaCode;

    if (entered)
    {
        // the first player inside owns it: if somebody was already there, we are his guest
        playerid_t owner = FindLowestOtherPlayer(localPlayer, areaCode);
        if (owner.valid())
            BecomeGuest(owner);
        else
            BecomeOwner(true);
        return;
    }

    if (s_role == Role::Owner)
    {
        // D interior-handover: wait out the grace period before showing (and hosting) our own hidden population:
        // the peds of the owner that left are handed over to us during it
        if (s_nHandoverGraceUntil != 0)
        {
            if ((int32_t)(CConfigLoader::GetSyncTime() - s_nHandoverGraceUntil) < 0)
                return;

            s_nHandoverGraceUntil = 0;

            // at least one take-over arrived: the peds of the previous owner are ours, keep ours hidden
            if (!s_bGotTakeover)
                RestoreHiddenPeds(s_nAreaCode != 0);
            return;
        }

        if (!s_bOwnerUnconfirmed)
            return;

        // two players entered in the same interval and neither saw the other inside: the lowest player id owns
        playerid_t other = FindLowestOtherPlayer(localPlayer, areaCode);
        if (other.valid() && CNetworkPlayerManager::m_localPlayerId.valid() &&
            other.value() < CNetworkPlayerManager::m_localPlayerId.value())
        {
            BecomeGuest(other);
            return;
        }

        s_bOwnerUnconfirmed = false;
        return;
    }

    if (s_role != Role::Guest)
        return;

    // the owner left the interior or disconnected while we are still inside: the lowest id still inside owns it
    if (s_ownerId.valid() && IsPlayerInInterior(s_ownerId, localPlayer, areaCode))
    {
        s_nOwnerMissingTicks = 0;
        return;
    }

    // one missing packet is not enough: give the owner a full interval before taking the interior from him
    if (++s_nOwnerMissingTicks < 2)
        return;

    s_nOwnerMissingTicks = 0;

    uint32_t lowest = FindLowestPlayerIncludingLocal(localPlayer, areaCode);
    if (CNetworkPlayerManager::m_localPlayerId.valid() && lowest == CNetworkPlayerManager::m_localPlayerId.value())
        BecomeOwner(false);
    else if (lowest != 0xFFFFFFFFu)
        BecomeGuest(playerid_t(lowest));
}

bool CInteriorOwnership::ShouldHideLocalPed(CPed* ped)
{
    if (s_role != Role::Guest || !IsValidPed(ped))
        return false;

    // never touch players (local or remote network players)
    if (ped->IsPlayer() || ped->m_nPedType <= PED_TYPE_PLAYER_UNUSED || CNetworkPlayerManager::GetPlayer(ped))
        return false;

    // the peds of the owner are remote records: they are shown normally
    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    if (networkPed && !networkPed->m_bSyncing)
        return false;

    // mission peds are shared (coop) or stay local to their player (individual mission)
    if (IsMissionPed(ped))
        return false;

    // a shopkeeper only hides behind the owner's copy: without it the shop has nobody serving (pizzeria, 2026-11-13)
    if (IsServicePed(ped) && !IsRepresentedRemotely(ped))
        return false;

    return IsInLocalInterior(ped, FindPlayerPed(0));
}

bool CInteriorOwnership::IsRepresentedRemotely(CPed* ped)
{
    if (!IsValidPed(ped))
        return false;
    for (auto record : CNetworkPedManager::m_pPeds)
    {
        if (record->m_bSyncing || record->m_pPed == ped || record->m_nModelId != ped->m_nModelIndex ||
            (record->m_nAreaCode && record->m_nAreaCode != s_nAreaCode))
            continue;
        const CVector pos = IsValidPed(record->m_pPed) ? record->m_pPed->GetPosition() : record->m_vecLastPos;
        if (DistanceBetweenPoints(pos, ped->GetPosition()) < 4.0f)
            return true;
    }
    return false;
}

// Shopkeepers follow their stand-in: hidden while the owner's copy is there, shown again when it is gone.
void CInteriorOwnership::RefreshServicePeds()
{
    static uint32_t lastCheck = 0;
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    if (now - lastCheck < 500)
        return;
    lastCheck = now;
    servicePeds.remove(nullptr);
    for (CPed* ped : servicePeds)
    {
        if (!IsValidPed(ped) || CNetworkPedManager::GetPed(ped))
            continue;
        const bool hidden = std::find(s_hiddenPeds.begin(), s_hiddenPeds.end(), ped) !=
                            s_hiddenPeds.end();
        if (!hidden && ShouldHideLocalPed(ped))
            HideLocalPed(ped);
        else if (hidden && !IsRepresentedRemotely(ped))
        {
            auto& list = s_hiddenPeds;
            for (auto it = list.begin(); it != list.end(); ++it)
                if (*it == ped)
                {
                    ped->m_bIsVisible = true;
                    ped->m_bUsesCollision = true;
                    ped->CleanUpOldReference((CEntity**)&*it);
                    list.erase(it);
                    CMissionMirror::LogEventf("interior-service-show model=%d area=%u", ped->m_nModelIndex, ped->m_nAreaCode);
                    break;
                }
        }
    }
}

void CInteriorOwnership::HideLocalPed(CPed* ped)
{
    if (!IsValidPed(ped))
        return;

    CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
    if (networkPed)
    {
        // a remote ped: not ours, leave it alone
        if (!networkPed->m_bSyncing)
            return;

        // we were hosting it: tell the others to remove their copy, the game ped stays here (hidden)
        CMissionMirror::LogEventf("interior-unpublish ped=%s area=%u", to_string(networkPed->m_pedId).c_str(), s_nAreaCode);
        CNetworkPedManager::Remove(networkPed);
        delete networkPed;
    }

    ped->m_bIsVisible = false;
    ped->m_bUsesCollision = false;

    for (CPed* hiddenPed : s_hiddenPeds)
    {
        if (hiddenPed == ped)
            return;
    }

    CMissionMirror::LogEventf("interior-hide model=%d area=%u service=%d", ped->m_nModelIndex, s_nAreaCode, IsServicePed(ped));
    s_hiddenPeds.push_back(ped);
    ped->RegisterReference((CEntity**)&s_hiddenPeds.back());
}

bool CInteriorOwnership::ShouldSuppressRemotePed(const CNetworkPed* networkPed)
{
    if (CConfigLoader::ms_bInteriorPopulationSync)
        return false; // the server rejects publications by guests; creator id survives a handover
    if (s_role != Role::Owner || s_nAreaCode == 0 || !networkPed || networkPed->m_bSyncing)
        return false;

    CPed* ped = networkPed->m_pPed;
    if (ped && (!ped->IsVTableValid() || ped->IsPlayer()))
        return false;

    if (IsAuthorityMissionPed(networkPed))
    {
        static uint32_t lastLog = 0;
        const uint32_t now = CConfigLoader::GetSyncTime();
        if (lastLog == 0 || now - lastLog >= 5000)
        {
            CMissionMirror::LogEventf("interior-mission-ped-keep area=%u suppressed=%d",
                s_nAreaCode, networkPed->m_bSuppressedDuplicate);
            lastLog = now;
        }
        // CStreamingSync clears suppression and streams mission records back in.
        return false;
    }

    // coop mission peds are shared by every client
    if (networkPed->m_nCreatedBy == MISSION_CHAR && COpCodeSync::IsSyncedMissionRunning())
        return false;

    if (networkPed->m_pedId.creator() == CNetworkPlayerManager::m_localPlayerId)
        return false;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return false;

    unsigned char pedArea = networkPed->m_nAreaCode;
    CVector pos = networkPed->m_vecLastPos;
    if (ped && ped->IsVTableValid() && ped->m_matrix)
    {
        if (ped->m_nAreaCode != 0)
            pedArea = ped->m_nAreaCode;
        pos = ped->GetPosition();
    }

    if (pedArea != 0)
        return pedArea == s_nAreaCode;

    // unknown area: interiors are far away from the main map, so this is our interior
    return DistanceBetweenPoints(pos, localPlayer->GetPosition()) < SAME_INTERIOR_RADIUS;
}
