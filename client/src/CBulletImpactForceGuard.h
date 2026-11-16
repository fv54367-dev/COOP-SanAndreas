#pragma once
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include <CPools.h>

// Snapshot only around the native impact: damage, tyres and effects stay native.
class CBulletImpactForceGuard
{
    CVehicle* m_pVehicle = nullptr;
    int m_nVehicleRef = 0;
    CVector m_vecMoveSpeed;
    CVector m_vecTurnSpeed;
    int m_nWeapon;
    bool m_bRemote;
    static inline DWORD ms_nLastLog = 0;
    static inline bool ms_bLogged = false;

public:
    CBulletImpactForceGuard(CEntity* victim, int weapon, bool remote)
        : m_nWeapon(weapon), m_bRemote(remote)
    {
        if (!CConfigLoader::ms_bRemoteBulletNoForce || !victim ||
            victim->m_nType != ENTITY_TYPE_VEHICLE || !CPools::ms_pVehiclePool)
            return;
        auto vehicle = static_cast<CVehicle*>(victim);
        if (!CPools::ms_pVehiclePool->IsObjectValid(vehicle) || !vehicle->IsVTableValid())
            return;
        m_pVehicle = vehicle;
        m_nVehicleRef = CPools::ms_pVehiclePool->GetRef(vehicle);
        m_vecMoveSpeed = vehicle->m_vecMoveSpeed;
        m_vecTurnSpeed = vehicle->m_vecTurnSpeed;
    }

    ~CBulletImpactForceGuard()
    {
        if (!m_pVehicle || !CPools::ms_pVehiclePool ||
            CPools::ms_pVehiclePool->GetAt(m_nVehicleRef) != m_pVehicle || !m_pVehicle->IsVTableValid())
            return;
        const float dv = (m_pVehicle->m_vecMoveSpeed - m_vecMoveSpeed).Magnitude();
        m_pVehicle->m_vecMoveSpeed = m_vecMoveSpeed;
        m_pVehicle->m_vecTurnSpeed = m_vecTurnSpeed;
        const DWORD now = GetTickCount();
        if (dv > 0.002f && (!ms_bLogged || now - ms_nLastLog >= 2000))
        {
            ms_bLogged = true;
            ms_nLastLog = now;
            CMissionMirror::LogEventf("bullet-force-cancelled weapon=%d model=%d remote=%d dv=%.3f",
                m_nWeapon, (int)m_pVehicle->m_nModelIndex, m_bRemote ? 1 : 0, dv);
        }
    }
};
