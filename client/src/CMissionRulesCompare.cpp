#include "CHighStakesRace.h"
#include "stdafx.h"
#include "CHomeInvasion.h"
#include <CFireManager.h>
#include "CJustBusinessEscort.h"
#include "CActivityChallenge.h"
#include "CPatientHandover.h"
#include "CCoopPool.h"
#include "CCoopLowrider.h"
#include "CMissionSyncRules.h"
#include "CMissionRulesCounters.h"
#include "CMissionRulesWorld.h"
#include "CMissionRulesGuestCars.h"
#include "CMissionRulesCompare.h"
#include "CMissionGuestCounts.h"
#include "CMissionGuestActions.h"
#include "CMissionMirror.h"
#include "CMirrorBlips.h"
#include "COpCodeSync.h"
#include "CCoopMission.h"
#include "CConfigLoader.h"
#include "Commands/Commands/CCommandTeleportPlayersToHostSafely.h"
#include "Commands/Commands/CCommandPutNetworkPlayersInCar.h"
#include "Commands/Commands/CCommandIsAnyNetworkPlayerInArea3D.h"
#include "Commands/CommandHooks.h"
#include "UI/CChat.h"
#include "CMissionMode.h"
#include "CMissionLock.h"
#include "CMissionHints.h"
#include "CMissionSceneWait.h"
#include "CNetworkStaticBlip.h"
#include "network/packets/mission_wait_notice.h"
#include "network/packets/scripts.h"
#include "CPedGroups.h"
#include "CPedIntelligence.h"
#include "CEntryExit.h"
#include "CEntryExitManager.h"
#include "CEntryExitMarkerSync.h"
#include "CCutsceneMgr.h"

namespace mission_compare {
using namespace mission_guest_cars;

// Remote cameras are unknown: nearby mission players might see the entity.
static constexpr float ON_SCREEN_SHARED_RADIUS = 120.0f;
// INTRO1 moves the Ballas car (51@) 40 m behind the host when it is 50 m away and off his screen (INTRO1.txt:1344): a
// guest this near may be looking at it, so the jump is not done
static constexpr float ON_SCREEN_SHARED_CAR_RADIUS = 250.0f;

DWORD nBeachArrivalTick = 0;
static bool bBeachArrived = false;

static bool IsPedNearMissionLocate(CPed* ped, const CVector& center, float radius, float height, bool b3D,
    unsigned int areaCode, float minReach = 12.0f)
{
    const float reach = (std::max)(radius * 3.0f, minReach);
    const float reachZ = (std::max)(height * 3.0f, 10.0f);
    const CVector pos = ped->GetPosition();
    const float dx = pos.x - center.x, dy = pos.y - center.y;
    return dx * dx + dy * dy <= reach * reach && (!b3D || fabsf(pos.z - center.z) <= reachZ) &&
        ped->m_nAreaCode == areaCode;
}

// the players of the mission that are not near the area of a LOCATE on the local player (names for the chat)
static bool AreMissionPlayersNear(const CVector& center, float radius, float height, bool b3D, std::string& missing,
    float minReach = 12.0f)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!networkPlayer->m_pPed || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;
        const bool bNear = !CMissionLock::IsIndependentIntro(networkPlayer->m_nRunningMission) &&
            IsPedNearMissionLocate(networkPlayer->m_pPed, center, radius, height, b3D, localPlayer->m_nAreaCode, minReach);
        if (!bNear)
        {
            if (!missing.empty())
                missing += ", ";
            missing += networkPlayer->GetName();
        }
    }
    return missing.empty();
}

// the MAIN scripts that start story missions (each one's LOAD_AND_LAUNCH_MISSION ids, checked in the compiled main.scm,
// ours and the original: same global counters). CESAR and BCESAR also start the race tournament (35), a VS battle
// where each player runs his own copy: not here.
static const char* const aszStoryLaunchers[] = {"INT", "SWEET", "CRASH", "RYDER", "SMOKE", "STRAP", "LA1FIN", "BCRASH", "CAT",
    "TRU", "GARAGE", "SCRASH", "WUZI", "SYND", "STEAL", "ZERO", "DESERT", "CASINO", "VCRASH", "DOC", "HEIST", "MANSION",
    "GROVE", "RIOT"};

static bool IsStoryLauncher(CRunningScript* script)
{
    if (script->m_bIsMission)
        return false;
    for (const char* szName : aszStoryLaunchers)
    {
        if (_strnicmp(script->m_szName, szName, 8) == 0)
            return true;
    }
    return false;
}

// CAN_PLAYER_START_MISSION (03EE), which every launcher checks right after its start marker: the coop start rules the
// Coop.LocateAllPlayersOnFoot3D of SWEET and INT apply, for every story launcher. In coop only the script authority
// starts the mission and only with every player of the mission next to him; alone (F1 individual, everybody else
// individual, Cesar Vialpando) he starts it as in the original game. The story mission lock is checked as well.
static bool FilterMissionStart(CRunningScript* script)
{
    // ours: SWEET and INT already did all of this in Coop.LocateAllPlayersOnFoot3D
    const bool bSweet = _strnicmp(script->m_szName, "SWEET", 8) == 0;
    if (!CConfigLoader::ms_bVanillaScripts && (bSweet || _strnicmp(script->m_szName, "INT", 8) == 0))
        return true;

    // original INT asks CAN_PLAYER_START_MISSION at CJ's door and at Ryder's before it checks which of the two is due:
    // walking past CJ's door with Ryder pending said "waiting for..." (testeo 14-11). Not the due spot: no wait, and the
    // script itself launches nothing there
    if (CConfigLoader::ms_bVanillaScripts && _strnicmp(script->m_szName, "INT", 8) == 0)
    {
        const int counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
        const float* spot = reinterpret_cast<const float*>(&CTheScripts::ScriptSpace[counter == 0 ? 1836 : 1860]);
        const CVector at = FindPlayerCoors(0);
        if (counter < 0 || counter > 1 || fabsf(at.x - spot[0]) > 4.0f || fabsf(at.y - spot[1]) > 4.0f)
            return true;
    }

    // the Sweet counter ($flag_sweet_mission_counter, ScriptSpace+1808 in both main.scm): 6 launches Cesar Vialpando
    const int sweetCounter = bSweet ? *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808]) : -1;
    if (bSweet && sweetCounter >= 0 && sweetCounter <= 255)
        CMissionMode::UpdateSweetProgress(sweetCounter);
    const bool bIndividualOnly = bSweet && sweetCounter == 6 &&
        !(CConfigLoader::ms_bCoopLowrider && CConfigLoader::ms_bVanillaScripts);
    const int missionId = (bSweet && sweetCounter >= 0 && sweetCounter <= 6) ? 13 + sweetCounter : -1;

    if (CMissionLock::IsLaunchBlocked(script, missionId))
        return false;

    const bool bAlone = CMissionMode::IsPlayingAlone() || bIndividualOnly || CCoopMission::IsLocalIndividualMission();
    if (bAlone)
        return true;

    // the launcher never reaches its own "come back at..." text from here on: show it to this player
    CMissionHints::OnLauncherAsked(script);

    // every story chain: the players of the mission have to be at the same mission of it, each one by his own progress
    // (a guest that passed Home Invasion alone started it again with a host that had not, 12.11)
    {
        std::string other;
        int theirs = 0, mine = 0;
        const int chain = CMissionMode::LauncherChain(script->m_szName);
        if (CConfigLoader::ms_bVanillaScripts && CMissionMode::FindChainMismatch(chain, other, theirs, mine))
        {
            CMissionMode::ShowChainMismatch(chain, other, theirs, mine);
            return false;
        }
    }

    std::string name;
    int theirCounter = 0;
    if (bSweet && !CConfigLoader::ms_bVanillaScripts && CMissionMode::FindSweetMismatch(sweetCounter, name, theirCounter))
    {
        CMissionMode::ShowProgressMismatch(name, theirCounter, sweetCounter);
        return false;
    }

    std::string missing;
    // a coop guest never starts it: the authority runs it for everybody. The authority only tells who is missing when
    // he is in the marker himself: a guest here alone sees who he waits for.
    if (!CCoopMission::IsScriptAuthority())
    {
        bool bAllHere = AreMissionPlayersNear(FindPlayerCoors(0), 1.0f, 1.0f, true, missing, 4.0f);
        // The host's own launcher only asks with him inside its small marker (1.2 m): 4 m from us is not enough. Next to
        // the guest but off the marker, neither game said anything and the mission never started (Cesar Vialpando, 14-11).
        if (bAllHere && !CCoopMission::IsRunning())
        {
            const CVector at = FindPlayerCoors(0);
            for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (!networkPlayer->m_bIsHost || !networkPlayer->m_pPed || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
                    continue;
                const CVector pos = networkPlayer->m_pPed->GetPosition();
                if ((pos.x - at.x) * (pos.x - at.x) + (pos.y - at.y) * (pos.y - at.y) > 2.0f * 2.0f)
                {
                    missing = networkPlayer->GetName();
                    bAllHere = false;
                }
            }
        }
        if (!bAllHere)
            CMissionHints::LocalStartWait(missing);
        static DWORD nLastGuestLog = 0;
        if (GetTickCount() - nLastGuestLog > 5000)
        {
            nLastGuestLog = GetTickCount();
            CMissionMirror::LogEventf("rule-start-ask launcher=%.8s authority=0 missing=%s", script->m_szName, missing.c_str());
        }
        return false;
    }

    // the start marker is small: everybody within a few metres of him
    if (!AreMissionPlayersNear(FindPlayerCoors(0), 1.0f, 1.0f, true, missing, 4.0f))
    {
        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::START, missing);
        static DWORD nLastMessage = 0;
        if (GetTickCount() - nLastMessage > 5000)
        {
            nLastMessage = GetTickCount();
            if (!CMissionHints::WaitNoticesEnabled())
            {
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s en el marcador para empezar la mision.", missing.c_str());
                CMissionHints::Help("Esperando a " + missing + " en el marcador para empezar la mision.");
            }
            CMissionMirror::LogEventf("rule-start-wait launcher=%.8s missing=%s", script->m_szName, missing.c_str());
        }
        return false;
    }
    return true;
}

#include "CMissionSyncRules_Locates.inc"
#include "CMissionSyncRules_GuestCarFilters.inc"
#include "CMissionSyncRules_MissionFilters.inc"


void InjectCompareHook()
{
    // mov al, [ecx + 0xD2]: only patched while the 1.0 US bytes are there
    const uint8_t expected[] = {0x8A, 0x81, 0xD2, 0x00, 0x00, 0x00};
    if (memcmp(reinterpret_cast<const void*>(0x4859D0), expected, sizeof(expected)) == 0)
    {
        patch::RedirectJump(0x4859D0, CRunningScript__UpdateCompareFlag_Hook);
        patch::Nop(0x4859D5, 1);
    }
}

}  // namespace mission_compare
