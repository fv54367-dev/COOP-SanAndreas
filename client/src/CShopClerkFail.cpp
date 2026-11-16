#include "stdafx.h"
#include "CShopClerkFail.h"
#include "CConfigLoader.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "CMissionHints.h"
#include "UI/CChat.h"
#include <CPools.h>
#include <CTheScripts.h>

namespace
{
int s_nClerkRef = 0;       // pool handle of the shop clerk while a shop script runs
bool s_bFailedHere = false; // one fail per shop visit

// shopkeeper models of CLOTH.txt (211, 217) and BARB.txt (156, 176, 177); the script creates them with Char.Create
bool IsClerkModel(int model, bool clothes)
{
    return clothes ? (model == 211 || model == 217) : (model == 156 || model == 176 || model == 177);
}
}  // namespace

void CShopClerkFail::Process()
{
    CPlayerPed* player = FindPlayerPed(0);
    bool clothes = false, shopRuns = false;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (_strnicmp(script->m_szName, "CLOTH", 8) == 0)
            shopRuns = clothes = true;
        else if (_strnicmp(script->m_szName, "BARB", 8) == 0)
            shopRuns = true;
    }

    if (!CConfigLoader::ms_bShopClerkFail || !CConfigLoader::ms_bVanillaScripts || !CNetwork::m_bAuthenticated || !player ||
        !shopRuns || !CCoopMission::IsScriptAuthority() || CMissionMirror::GetRunningMissionId() < 0 ||
        !CTheScripts::IsPlayerOnAMission())
    {
        s_nClerkRef = 0;
        s_bFailedHere = !shopRuns && s_bFailedHere ? false : s_bFailedHere;
        return;
    }

    if (s_nClerkRef == 0)
    {
        // the clerk: the script ped of the shop model closest to the player (the shop scripts create it when the shop loads)
        float best = 400.0f;
        for (int i = 0; CPools::ms_pPedPool && i < CPools::ms_pPedPool->m_nSize; ++i)
        {
            CPed* ped = CPools::ms_pPedPool->GetAt(i);
            if (!ped || ped == player || ped->m_nCreatedBy != MISSION_CHAR || ped->m_fHealth <= 0.0f ||
                !IsClerkModel(ped->m_nModelIndex, clothes))
                continue;
            const float dist = (ped->GetPosition() - player->GetPosition()).Magnitude();
            if (dist < best)
            {
                best = dist;
                s_nClerkRef = CPools::GetPedRef(ped);
            }
        }
        return;
    }

    CPed* clerk = CPools::GetPed(s_nClerkRef);
    if (!clerk)
    {
        s_nClerkRef = 0;  // the script removed it (end of the visit): not a death
        return;
    }
    if (clerk->m_fHealth > 0.0f && clerk->m_ePedState != PEDSTATE_DEAD)
        return;

    s_nClerkRef = 0;
    if (s_bFailedHere)
        return;
    s_bFailedHere = true;
    CMissionMirror::LogEventf("rule-shop-clerk-dead script=%s", clothes ? "CLOTH" : "BARB");
    CChat::AddMessage("{c7a6ff}[Negocio]{ffffff} %s: la mision fracasa para todos",
        clothes ? "Mataron a la empleada de la tienda de ropa" : "Mataron al peluquero");
    CMissionHints::Help(clothes ? "Mataron a la empleada: la mision fracasa." : "Mataron al peluquero: la mision fracasa.");
    CCoopMission::RequestMissionFail();
}
