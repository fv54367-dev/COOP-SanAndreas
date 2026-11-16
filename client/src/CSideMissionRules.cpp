#include "CHighStakesRace.h"
#include "stdafx.h"
#include "CActivityChallenge.h"
#include "CSideMissionRules.h"
#include "CCoopMission.h"
#include "CMissionMirror.h"
#include "COpCodeSync.h"
#include "CConfigLoader.h"
#include <CTimer.h>
#include <vector>

struct SSideMission
{
    const char* m_szName;
    const char* m_szTitle;
};

// the 21 side missions whose scripts called Coop.ChooseSideMissionMode, with the same titles
static const SSideMission aSideMissions[] = {{"AMBULAN", "Paramedico"}, {"BCOUR", "Mensajero"}, {"BLOOD", "Blood Ring"},
    {"BOAT", "Escuela de botes"}, {"BSKOOL", "Escuela de motos"}, {"BURGJB", "Robo"}, {"COPCAR", "Vigilante"},
    {"CPRACE", "Carrera"}, {"DESERT5", "Escuela de vuelo"}, {"DSKOOL", "Escuela de manejo"}, {"FIRETRU", "Bombero"},
    {"FREIGHT", "Tren"}, {"KICKSTA", "Kickstart"}, {"MTBIKER", "Chiliad"}, {"PIMP", "Proxeneta"}, {"QUARRY", "Cantera"},
    {"SHRANGE", "Ammu-Nation"}, {"STUNT", "Stunt"}, {"TAXIODD", "Taxi"}, {"TRIA", "Beat the Cock"}, {"TRUCK", "Camionero"}};

struct SHeldScript
{
    CRunningScript* m_pScript;
    const SSideMission* m_pMission;
};

static std::vector<SHeldScript> vHeld;
static bool bVigilanteRewardSent = false;

static bool IsActiveScript(CRunningScript* pScript)
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script == pScript)
            return true;
    }
    return false;
}

static void RemoveSyncedName(const char* szName)
{
    for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
    {
        if (_strnicmp(COpCodeSync::ms_aszSyncedScripts[i], szName, 7) != 0)
            continue;
        // keep the table packed: the last one takes its place
        --COpCodeSync::ms_iFreeSyncedScript;
        if (i != COpCodeSync::ms_iFreeSyncedScript)
            memcpy(COpCodeSync::ms_aszSyncedScripts[i], COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript], 8);
        return;
    }
}

bool CSideMissionRules::IsEnabled()
{
    return CConfigLoader::ms_bVanillaScripts && CConfigLoader::ms_bCoopSideMissions;
}

static void OnVigilanteReward(CRunningScript* pScript)
{
    if (bVigilanteRewardSent || _strnicmp(pScript->m_szName, "COPCAR", 8) != 0 ||
        !CNetwork::m_bConnected || !CCoopMission::IsRunning() || !CCoopMission::IsOwner() ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript) ||
        ScriptParams[0] != 0 || ScriptParams[1] != 50)
        return;

    // Original COPCAR: 055F (+50), then $done_copcar_progress = 1 (global byte offset 0x1740).
    // gta-reversed PlayerCommands.cpp: IncreasePlayerMaxArmour also fills the player's armour.
    constexpr int32_t doneFlagOffset = 5952;
    if (*reinterpret_cast<int32_t*>(&CTheScripts::ScriptSpace[doneFlagOffset]) != 0)
        return;

    bVigilanteRewardSent = true;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;
        Packets::Scripts::OddjobReward packet{};
        packet.playerid = networkPlayer->m_playerId;
        packet.rewardId = 1;
        packet.doneFlagOffset = doneFlagOffset;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("rule-oddjob-reward script=%.8s player=%u reward=1 flag=%d", pScript->m_szName,
            packet.playerid.value(), doneFlagOffset);
    }
}

void CSideMissionRules::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    if (CHighStakesRace::Enabled() && pScript && pScript->m_bIsMission && (opcode & 0x7FFF) == 0x03A4 &&
        !COpCodeSync::bProcessingNetworkOpcode && _strnicmp(pScript->m_szName, "CPRACE", 8) == 0) {
        RemoveSyncedName("CPRACE");
        if (CHighStakesRace::StartRace(pScript) || CHighStakesRace::IsStoryLaunch(pScript)) return;
    }
    if (!IsEnabled() || !pScript || !pScript->m_bIsMission || COpCodeSync::bProcessingNetworkOpcode)
        return;

    // Kickstart contests run independently, including the first 20 seconds before U.
    if (CActivityChallenge::Independent(pScript)) { RemoveSyncedName("KICKSTA"); RemoveSyncedName("BLOOD"); RemoveSyncedName("TRIA"); return; }
    opcode &= 0x7FFF;
    if (opcode == 0x055F)
    {
        OnVigilanteReward(pScript);
        return;
    }
    if (opcode != 0x03A4)
        return;
    if (_strnicmp(pScript->m_szName, "COPCAR", 8) == 0)
        bVigilanteRewardSent = false;

    for (const SSideMission& mission : aSideMissions)
    {
        if (_strnicmp(pScript->m_szName, mission.m_szName, 7) != 0)
            continue;

        for (const SHeldScript& held : vHeld)
        {
            if (held.m_pScript == pScript)
                return;
        }
        // a name left synced by an earlier coop run would sync it before the choice
        RemoveSyncedName(mission.m_szName);
        vHeld.push_back({pScript, &mission});
        CMissionMirror::LogEventf("side-hold script=%.8s", mission.m_szName);
        return;
    }
}

void CSideMissionRules::Process()
{
    for (auto it = vHeld.begin(); it != vHeld.end();)
    {
        CRunningScript* pScript = it->m_pScript;
        if (!IsActiveScript(pScript))
        {
            it = vHeld.erase(it);
            continue;
        }

        if (!CCoopMission::ChooseSideMissionMode(pScript, it->m_pMission->m_szName, it->m_pMission->m_szTitle))
        {
            // still deciding: the script does not run this frame
            pScript->m_nWakeTime = CTimer::m_snTimeInMilliseconds + 1000;
            ++it;
            continue;
        }

        // decided: coop renamed and synced it (CCoopMission::ResolveWaitingChoices); individual must not be synced
        if (!COpCodeSync::IsScriptSynced(pScript))
        {
            char szSolo[8]{};
            strncpy_s(szSolo, it->m_pMission->m_szName, 3);
            strcat_s(szSolo, "SOLO");
            strncpy_s(pScript->m_szName, 8, szSolo, 7);
        }
        pScript->m_nWakeTime = 0;
        CMissionMirror::LogEventf("side-release script=%.8s synced=%d", pScript->m_szName, COpCodeSync::IsScriptSynced(pScript) ? 1 : 0);
        it = vHeld.erase(it);
    }
}
