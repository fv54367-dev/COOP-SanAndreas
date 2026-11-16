#include "stdafx.h"
#include "CNetworkPadSync.h"
#include "CCoopPool.h"
#include "COpCodeSync.h"
#include <CCoopMission.h>

// scripts whose second pad is driven by the network player
static const char* ms_aszTwoPlayerScripts[] = {"POOL2"};

static bool ms_bInjecting = false;

static bool IsTwoPlayerScriptRunning()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        for (const char* name : ms_aszTwoPlayerScripts)
        {
            if (strnicmp(script->m_szName, name, 7) == 0 && COpCodeSync::IsScriptSynced(script))
                return true;
        }
    }
    return false;
}

static void InjectSecondPad()
{
    if (CCoopPool::Enabled()) return; // T55 owns pad 1 only inside native POOL2 commands.
    CPad* pPad = CPad::GetPad(1);
    if (!pPad)
        return;

    CNetworkPlayer* pPlayer = nullptr;
    if (CNetwork::m_bAuthenticated && CCoopMission::IsScriptAuthority() && IsTwoPlayerScriptRunning())
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer->m_pPed && CCoopMission::ShouldIncludeNetworkPlayer(pNetworkPlayer))
            {
                pPlayer = pNetworkPlayer;
                break;
            }
        }
    }

    if (!pPlayer)
    {
        if (ms_bInjecting)
        {
            pPad->OldState = CControllerState{};
            pPad->NewState = CControllerState{};
            ms_bInjecting = false;
        }
        return;
    }

    pPad->OldState = pPlayer->m_rawOldControllerState;
    pPad->NewState = pPlayer->m_rawNewControllerState;
    ms_bInjecting = true;
}

void CNetworkPadSync::Init()
{
    // right before CTheScripts::Process, after the pads were updated
    Events::processScriptsEvent.before += InjectSecondPad;
}
