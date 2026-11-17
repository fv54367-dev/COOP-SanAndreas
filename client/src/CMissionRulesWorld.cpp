#include "stdafx.h"
#include "CMissionRulesWorld.h"
#include "CMissionMirror.h"
#include "CCoopMission.h"
#include <CFireManager.h>

// BCRASH1 (coop-badlands): the witness (284@) is watched and chased by every mission player, not only the host.
// IS_LINE_OF_SIGHT_CLEAR from the host's coords (15/30 s out of sight = lost), LOCATE_CHAR_ANY_MEANS_CHAR_3D
// host-witness 100/25 m (lost), _2D 10 m (he flees) and LOCATE_CHAR_ANY_MEANS_3D 100x100x30 around his body (gone
// without the photo). The 4-5 m locates stay host-only. Raw results, before the native NOT.
bool CMissionRulesWorld::Badlands(uint16_t opcode)
{
    auto* host = FindPlayerPed(0);
    if (!host) return false;
    const float* f = reinterpret_cast<const float*>(ScriptParams);
    const int hostRef = CPools::GetPedRef(host);
    // GET_CHAR_COORDINATES gives the vehicle's position for a char in one
    const auto at = [](CPed* ped) {
        return ped->m_nPedFlags.bInVehicle && ped->m_pVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
    };
    const auto witness = [&](int ref) -> CPed* {
        auto* ped = ScriptParams[0] == hostRef ? CPools::GetPed(ref) : nullptr;
        return ped && !ped->IsPlayer() && !CNetworkPlayerManager::GetPlayer(ped) ? ped : nullptr;
    };
    CVector center, los;
    float rx = 0.0f, ry = 0.0f, rz = -1.0f;  // rz < 0: 2D
    switch (opcode)
    {
    case 0x06BD:  // IS_LINE_OF_SIGHT_CLEAR x1 y1 z1 x2 y2 z2 buildings vehicles peds objects dummies
        if (DistanceBetweenPoints(CVector(f[0], f[1], f[2]), at(host)) > 1.0f) return false;
        los = CVector(f[3], f[4], f[5]);
        break;
    case 0x0104:  // LOCATE_CHAR_ANY_MEANS_CHAR_3D char char rx ry rz sphere
    {
        auto* target = witness(ScriptParams[1]);
        if (!target || ScriptParams[5] != 0 || f[2] != f[3] || f[2] != f[4] || (f[2] != 100.0f && f[2] != 25.0f))
            return false;
        center = at(target); rx = ry = rz = f[2];
        break;
    }
    case 0x00F2:  // LOCATE_CHAR_ANY_MEANS_CHAR_2D char char rx ry sphere
    {
        auto* target = witness(ScriptParams[1]);
        if (!target || ScriptParams[4] != 0 || f[2] != 10.0f || f[3] != 10.0f) return false;
        center = at(target); rx = ry = f[2];
        break;
    }
    case 0x00FE:  // LOCATE_CHAR_ANY_MEANS_3D char x y z rx ry rz sphere
        if (ScriptParams[0] != hostRef || ScriptParams[7] != 0 || f[4] != 100.0f || f[5] != 100.0f || f[6] != 30.0f)
            return false;
        center = CVector(f[1], f[2], f[3]); rx = f[4]; ry = f[5]; rz = f[6];
        break;
    default:
        return false;
    }
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        auto* ped = guest->m_pPed;
        if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
            ped->m_nAreaCode != host->m_nAreaCode || (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
        const CVector pos = at(ped);
        bool counts;
        if (opcode == 0x06BD)
            counts = CWorld::GetIsLineOfSightClear(pos, los, ScriptParams[6] != 0, ScriptParams[7] != 0,
                ScriptParams[8] != 0, ScriptParams[9] != 0, ScriptParams[10] != 0, false, false);
        else
            counts = fabsf(pos.x - center.x) <= rx && fabsf(pos.y - center.y) <= ry &&
                (rz < 0.0f || fabsf(pos.z - center.z) <= rz);
        if (!counts) continue;
        static DWORD nLastLog = 0;
        if (GetTickCount() - nLastLog >= 5000)
        {
            nLastLog = GetTickCount();
            CMissionMirror::LogEventf("rule-badlands opcode=%04X player=%s", opcode, guest->GetName().c_str());
        }
        return true;
    }
    return false;
}

// coop-countryside conditions on the host (raw results, before NOT):
// TRUTH1: IS_CHAR_IN_CAR / IS_CHAR_SITTING_IN_CAR host + the combine (532): a guest driving it counts (TRUTH1.txt 1552,
// 1571, 1598), or the guest who took it from the farmer left the mission stuck on "get back in".
// TRUTH2: IS_CLOSEST_OBJECT_OF_TYPE_SMASHED_OR_DAMAGED x y z 1.0 #GRASSPLANT: a guest's flamethrower burns his own
// copy of the plant; his fire reaches us (CFireSync), so a burning fire on the plant counts.
bool CMissionRulesWorld::Countryside(CRunningScript* script, uint16_t opcode)
{
    auto* host = FindPlayerPed(0);
    if (!host) return false;
    const float* f = reinterpret_cast<const float*>(ScriptParams);
    if ((opcode == 0x00DB || opcode == 0x0448) && !_strnicmp(script->m_szName, "TRUTH1", 8))
    {
        auto* combine = CPools::GetVehicle(ScriptParams[1]);
        if (ScriptParams[0] != CPools::GetPedRef(host) || !combine || combine->m_nModelIndex != 532) return false;
        for (auto guest : CNetworkPlayerManager::m_pPlayers)
        {
            auto* ped = guest->m_pPed;
            if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                !ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != combine) continue;
            static DWORD nLastLog = 0;
            if (GetTickCount() - nLastLog >= 5000)
            {
                nLastLog = GetTickCount();
                CMissionMirror::LogEventf("rule-countryside combine player=%s", guest->GetName().c_str());
            }
            return true;
        }
        return false;
    }
    if (opcode == 0x0716 && !_strnicmp(script->m_szName, "TRUTH2", 8) && f[3] == 1.0f)
    {
        const CVector plant(f[0], f[1], f[2]);
        for (auto& fire : gFireManager.m_aFires)
        {
            if (!fire.m_nFlags.bActive) continue;
            const CVector d = fire.m_vecPosition - plant;
            if (d.x * d.x + d.y * d.y <= 2.0f * 2.0f && fabsf(d.z) <= 3.0f)
            {
                CMissionMirror::LogEventf("rule-countryside plant x=%.1f y=%.1f", plant.x, plant.y);
                return true;
            }
        }
    }
    return false;
}

