#include "stdafx.h"
#include "CSniperLaser.h"

#include <CCoronas.h>

namespace
{
constexpr float LASER_RANGE = 300.0f;

// arbitrary but unique id per player slot (CRegisteredCorona::m_dwId is just a lookup key)
constexpr uint32_t LASER_CORONA_ID_BASE = 0x534E0000;  // 'SN'
}  // namespace

void CSniperLaser::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* pPed = pPlayer->m_pPed;
        if (pPed == nullptr)
            continue;

        const Packets::Players::PlayerCameraSync& camera = pPlayer->m_cameraSnapshot;
        if (camera.cameraMode != (uint16_t)MODE_SNIPER)
            continue;

        if (pPed->m_aWeapons[pPed->m_nActiveWeaponSlot].m_eWeaponType != WEAPON_SNIPERRIFLE)
            continue;

        CVector source = camera.source;
        CVector target = source + camera.front * LASER_RANGE;

        CColPoint colPoint{};
        CEntity* pEntity = nullptr;

        // the laser must not stop on the sniper's own ped
        CEntity* pPreviousIgnoreEntity = CWorld::pIgnoreEntity;
        CWorld::pIgnoreEntity = pPed;
        bool bHit = CWorld::ProcessLineOfSight(
            source, target, colPoint, pEntity, true, true, true, true, true, false, false, true);
        CWorld::pIgnoreEntity = pPreviousIgnoreEntity;

        if (!bHit)
            continue;

        CCoronas::RegisterCorona(LASER_CORONA_ID_BASE + (uint32_t)pPlayer->m_nSlot, nullptr, 255, 0, 0, 255,
            colPoint.m_vecPoint, 0.15f, LASER_RANGE, CORONATYPE_SHINYSTAR, FLARETYPE_NONE, false, false, 0, 0.0f,
            true, 0.5f, 0, 1.0f, false, false);
    }
}
