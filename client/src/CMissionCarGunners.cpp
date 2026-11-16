#include "stdafx.h"
#include "CMissionCarGunners.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CNetworkPlayerManager.h"
#include <cmath>

namespace
{
// gta-reversed eWeaponType WEAPON_UNIDENTIFIED: m_nSavedWeapon holds nothing
constexpr auto NO_SAVED_WEAPON = static_cast<eWeaponType>(55);
bool attached = false;
DWORD lastTry = 0;
DWORD lastWant = 0;
// the guest's own rounds of the gunner weapon when he already had it (AttachPedToEntity gives 30000, the detach empties it)
eWeaponType ownWeapon = WEAPON_UNARMED;
int ownAmmo = -1;

void Detach(CPlayerPed* me)
{
    if (!attached)
        return;
    attached = false;
    if (!me)
        return;
    if (me->m_pAttachedTo)
    {
        // gta-reversed Ped.cpp: neither attach nor detach clears the reference; a car deleted later would still null it
        me->m_pAttachedTo->CleanUpOldReference(reinterpret_cast<CEntity**>(&me->m_pAttachedTo));
        me->DettachPedFromEntity();
    }
    else if (me->m_nSavedWeapon != NO_SAVED_WEAPON)
    {
        // the car was deleted under him (end of mission, explosion): what DettachPedFromEntity would restore
        me->SetCurrentWeapon(me->m_nSavedWeapon);
        me->m_aWeapons[me->m_nActiveWeaponSlot].m_nTotalAmmo = static_cast<unsigned int>(me->m_nTurretAmmo);
        me->m_nSavedWeapon = NO_SAVED_WEAPON;
        me->m_nPedFlags.bIsStanding = true;
    }
    me->m_bUsesCollision = true;
    const CWeaponInfo* info = CWeaponInfo::GetWeaponInfo(ownWeapon, 1);
    if (info && ownAmmo >= 0 && me->m_aWeapons[info->m_nSlot].m_eWeaponType == ownWeapon)
        me->m_aWeapons[info->m_nSlot].m_nTotalAmmo = static_cast<unsigned int>(ownAmmo);
    ownWeapon = WEAPON_UNARMED;
    ownAmmo = -1;
    CMissionMirror::LogEventf("car-gunner detach");
}

bool IsGunWeapon(int type)
{
    return type >= WEAPON_PISTOL && type <= WEAPON_SNIPERRIFLE;
}
}  // namespace

void CMissionCarGunners::Process()
{
    CPlayerPed* me = FindPlayerPed(0);
    const bool enabled = CConfigLoader::ms_bCoopCarGunners && CConfigLoader::ms_bVanillaScripts && CNetwork::m_bConnected &&
        CCoopMission::IsRunning() && CCoopMission::IsLocalParticipant() && !CCoopMission::HasLocalLeft() &&
        !CCoopMission::IsScriptAuthority();
    CNetworkPlayer* owner = enabled ? CNetworkPlayerManager::GetPlayer(CCoopMission::GetOwnerId()) : nullptr;
    CEntity* target = owner && owner->m_pPed ? owner->m_pPed->m_pAttachedTo : nullptr;
    // a train has its own riders (Catalyst, CMissionGuestActions)
    const bool want = target && me && me->m_fHealth > 0.0f && !me->m_nPedFlags.bInVehicle &&
        target->m_nType == ENTITY_TYPE_VEHICLE && static_cast<CVehicle*>(target)->m_nVehicleSubType != VEHICLE_TRAIN &&
        static_cast<CVehicle*>(target)->m_fHealth > 0.0f && me->m_nAreaCode == target->m_nAreaCode &&
        (me->m_pAttachedTo == target || DistanceBetweenPoints(me->GetPosition(), target->GetPosition()) < 150.0f);
    const DWORD now = GetTickCount();
    if (!want)
    {
        // the owner's copy can lose its attachment for a frame or two (sync gap): keep the guest on the car meanwhile
        if (enabled && attached && me && me->m_pAttachedTo && me->m_fHealth > 0.0f && now - lastWant < 750)
            return;
        Detach(me);
        return;
    }
    lastWant = now;
    if (me->m_pAttachedTo == target)
        return;
    // the owner moved to another car: leave the old one first
    if (attached && me->m_pAttachedTo)
        Detach(me);
    // the reference was nulled (the car was replaced): restore before attaching again
    if (attached && !me->m_pAttachedTo)
        Detach(me);
    if (me->m_pAttachedTo || now - lastTry < 1000)
        return;
    lastTry = now;

    // the owner's own seat on the car (the script's 0, -0.5, 0.8 in DRUGS4) when his copy holds it
    CVector base(0.0f, -0.5f, 0.8f);
    const CVector& o = owner->m_pPed->m_vecTurretOffset;
    if (std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z) && std::abs(o.x) < 3.0f && std::abs(o.y) < 6.0f &&
        std::abs(o.z) < 3.0f)
        base = o;
    int rank = 0;
    for (auto player : CNetworkPlayerManager::m_pPlayers)
        if (player->m_pPed && player->m_playerId != CCoopMission::GetOwnerId() &&
            player->m_playerId < CNetworkPlayerManager::m_localPlayerId && CCoopMission::ShouldIncludeNetworkPlayer(player))
            ++rank;
    // beside the owner first, then a row further back
    const float x = base.x + ((rank % 2 == 0) ? 0.7f : -0.7f);
    const float y = base.y - 0.9f * static_cast<float>(rank / 2);
    const int ownerWeapon = owner->m_pPed->m_aWeapons[owner->m_pPed->m_nActiveWeaponSlot].m_eWeaponType;
    const int weapon = IsGunWeapon(ownerWeapon) ? ownerWeapon : WEAPON_AK47;
    const CWeaponInfo* info = CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(weapon), 1);
    for (int model : {info ? info->m_nModelId1 : -1, info ? info->m_nModelId2 : -1})
    {
        if (model > 0 && CStreaming::ms_aInfoForModel[model].m_nLoadState != LOADSTATE_LOADED)
        {
            CStreaming::RequestModel(model, eStreamingFlags::GAME_REQUIRED);
            CStreaming::LoadAllRequestedModels(false);
        }
    }
    ownWeapon = static_cast<eWeaponType>(weapon);
    ownAmmo = info && me->m_aWeapons[info->m_nSlot].m_eWeaponType == ownWeapon
        ? static_cast<int>(me->m_aWeapons[info->m_nSlot].m_nTotalAmmo) : -1;
    // gta-reversed Ped.cpp AttachPedToEntity: a player gets the weapon (30000 rounds) and keeps aiming freely
    me->AttachPedToEntity(target, CVector(x, y, base.z), static_cast<unsigned short>(owner->m_pPed->m_fTurretAngleA), 6.2831853f, static_cast<eWeaponType>(weapon));
    attached = true;
    CMissionMirror::LogEventf("car-gunner attach rank=%d at=%.1f,%.1f,%.1f weapon=%d model=%d ok=%d", rank, x, y, base.z,
        weapon, target->m_nModelIndex, me->m_pAttachedTo == target ? 1 : 0);
}
