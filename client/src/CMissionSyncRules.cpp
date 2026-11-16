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

static constexpr DWORD MIN_TELEPORT_INTERVAL_MS = 500;
static constexpr int MAX_TELEPORT_SLOTS = 3;
// Remote cameras are unknown: nearby mission players might see the entity.
static constexpr float ON_SCREEN_SHARED_RADIUS = 120.0f;
// INTRO1 moves the Ballas car (51@) 40 m behind the host when it is 50 m away and off his screen (INTRO1.txt:1344): a
// guest this near may be looking at it, so the jump is not done
static constexpr float ON_SCREEN_SHARED_CAR_RADIUS = 250.0f;

// the scripts in scm/scripts that call Coop.EnableSyncingThisScript (always on the line after script_name)
static const char* const aszSyncedScripts[] = {"BCESAR4", "BCRASH1", "CASIN10", "CASINO1", "CASINO2", "CASINO3",
    "CASINO4", "CASINO5", "CASINO6", "CASINO7", "CASINO9", "CAT1", "CAT2", "CAT3", "CAT4", "CATALIN", "CATCUT", "CESAR1",
    "CRASH1", "CRASH4", "DECON", "DES10", "DES3", "DESERT4", "DESERT6", "DESERT9", "DOC2", "DRIV2", "DRIV3", "DRIV6",
    "DRUGS1", "DRUGS3", "DRUGS4", "FARLIE4", "FINALEA", "FINALEB", "FINALEC", "GARAG1", "GROVE1", "GROVE2", "GUNS1",
    "HEIST1", "HEIST2", "HEIST3", "HEIST4", "HEIST5", "HEIST9", "HOODS5", "INT", "INTRO1", "INTRO2", "LA1FIN2", "MAF4",
    "MANSIO1", "MANSIO2", "MANSIO3", "MANSON5", "MUSIC1", "MUSIC2", "MUSIC3", "MUSIC5", "POOL2", "RIOT1", "RIOT2", "RYDER2",
    "RYDER3", "SCRASH2", "SCRASH3", "SMOKE2", "SMOKE3", "STEAL1", "STEAL2", "STEAL4", "STEAL5", "SWEET1", "SWEET1B",
    "SWEET2", "SWEET3", "SWEET4", "SWEET6", "SWEET7", "SYN1", "SYN2", "SYN3", "SYN5", "SYN6", "SYN7", "SYND4", "TORENO1", "TORENO2",
    "TRUTH1", "TRUTH2", "TWAR7", "VCR2", "VCRASH1", "WUZI1", "WUZI2", "WUZI5", "ZERO1", "ZERO2", "ZERO4"};

static bool bRegistered = false;
static bool bTeleportPending = false;
static DWORD nLastTeleport = 0;
// T97: a pending gather leaves out the players already this near the host (0 = everybody, as before)
static float fGatherSkipNear = 0.0f;
// T97 MUSIC1: when the arrival scene at the beach party started (its ground snap of the host follows in the same tick)
static DWORD nBeachArrivalTick = 0;
static bool bBeachArrived = false;

static void RegisterSyncedScripts()
{
    for (const char* szName : aszSyncedScripts)
    {
        bool bKnown = false;
        for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
        {
            if (_strnicmp(COpCodeSync::ms_aszSyncedScripts[i], szName, 7) == 0)
            {
                bKnown = true;
                break;
            }
        }
        if (bKnown || COpCodeSync::ms_iFreeSyncedScript >= ARRAY_SIZE(COpCodeSync::ms_aszSyncedScripts))
            continue;

        strncpy_s(COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript], 8, szName, 7);
        ++COpCodeSync::ms_iFreeSyncedScript;
    }
    bRegistered = true;
}

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

// The locate and area conditions a mission checks the player (or his vehicle) with, read from the collected parameters
// (sa_sbl order: self first, the drawSphere flag last)
struct SLocate
{
    bool bCarSelf = false;  // the first parameter is a vehicle
    bool b3D = false;
    bool bSphere = false;   // drawSphere: the game draws a marker there
    bool bFixedPlace = false;  // a coordinate or an area, not around another entity
    CVector center;
    float radius = 0.0f;
    float height = 0.0f;
};

static CVector EntityPosition(int ref, bool bCar)
{
    CEntity* pEntity = bCar ? static_cast<CEntity*>(CPools::GetVehicle(ref)) : static_cast<CEntity*>(CPools::GetPed(ref));
    return pEntity ? pEntity->GetPosition() : CVector(0.0f, 0.0f, -1000.0f);
}

static bool ReadLocate(uint16_t opcode, SLocate& locate)
{
    const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
    auto coord = [&](bool b3D, int sphere) {
        locate.b3D = b3D;
        locate.bFixedPlace = true;
        locate.center = CVector(f[1], f[2], b3D ? f[3] : 0.0f);
        locate.radius = (std::max)(f[b3D ? 4 : 3], f[b3D ? 5 : 4]);
        locate.height = b3D ? f[6] : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };
    auto entity = [&](bool b3D, bool bTargetCar, int sphere) {
        locate.b3D = b3D;
        locate.center = EntityPosition(ScriptParams[1], bTargetCar);
        locate.radius = (std::max)(f[2], f[3]);
        locate.height = b3D ? f[4] : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };
    auto area = [&](bool b3D, int sphere) {
        const int x2 = b3D ? 4 : 3;
        locate.bFixedPlace = true;
        locate.b3D = b3D;
        locate.center = CVector((f[1] + f[x2]) * 0.5f, (f[2] + f[x2 + 1]) * 0.5f, b3D ? (f[3] + f[6]) * 0.5f : 0.0f);
        locate.radius = (std::max)(fabsf(f[x2] - f[1]), fabsf(f[x2 + 1] - f[2])) * 0.5f;
        locate.height = b3D ? fabsf(f[6] - f[3]) * 0.5f : 0.0f;
        locate.bSphere = ScriptParams[sphere] != 0;
    };

    if (opcode >= 0x00EC && opcode <= 0x00F1)  // LOCATE_(STOPPED_)CHAR_*_2D
        coord(false, 5);
    else if (opcode >= 0x00FE && opcode <= 0x0103)  // LOCATE_(STOPPED_)CHAR_*_3D
        coord(true, 7);
    else if (opcode >= 0x00F2 && opcode <= 0x00F4)  // LOCATE_CHAR_*_CHAR_2D
        entity(false, false, 4);
    else if (opcode >= 0x0104 && opcode <= 0x0106)  // LOCATE_CHAR_*_CHAR_3D
        entity(true, false, 5);
    else if (opcode >= 0x0202 && opcode <= 0x0204)  // LOCATE_CHAR_*_CAR_2D
        entity(false, true, 4);
    else if (opcode >= 0x0205 && opcode <= 0x0207)  // LOCATE_CHAR_*_CAR_3D
        entity(true, true, 5);
    else if (opcode == 0x0471 || opcode == 0x0473 || opcode == 0x0474 || opcode == 0x0476)
    {
        const bool b3D = opcode == 0x0474 || opcode == 0x0476;
        locate.b3D = b3D;
        CObject* object = CPools::GetObject(ScriptParams[1]);
        locate.center = object ? object->GetPosition() : CVector(0.0f, 0.0f, -1000.0f);
        locate.radius = (std::max)(f[2], f[3]);
        locate.height = b3D ? f[4] : 0.0f;
        locate.bSphere = ScriptParams[b3D ? 5 : 4] != 0;
    }
    else if (opcode == 0x00A3 || (opcode >= 0x01A1 && opcode <= 0x01A5))  // IS_CHAR_(STOPPED_)IN_AREA_*_2D
        area(false, 5);
    else if (opcode == 0x00A4 || (opcode >= 0x01A6 && opcode <= 0x01AA))  // IS_CHAR_(STOPPED_)IN_AREA_*_3D
        area(true, 7);
    else if (opcode == 0x01AD || opcode == 0x01AE)  // LOCATE_(STOPPED_)CAR_2D
    {
        locate.bCarSelf = true;
        coord(false, 5);
    }
    else if (opcode == 0x01AF || opcode == 0x01B0)  // LOCATE_(STOPPED_)CAR_3D
    {
        locate.bCarSelf = true;
        coord(true, 7);
    }
    else if (opcode == 0x00B0 || opcode == 0x01AB)  // IS_CAR_(STOPPED_)IN_AREA_2D
    {
        locate.bCarSelf = true;
        area(false, 5);
    }
    else if (opcode == 0x00B1 || opcode == 0x01AC)  // IS_CAR_(STOPPED_)IN_AREA_3D
    {
        locate.bCarSelf = true;
        area(true, 7);
    }
    else
        return false;
    return true;
}

// the players of the mission in another place than the authority: another interior, or outside (names for the chat)
static bool FindPlayersInOtherPlace(unsigned int areaCode, std::string& missing, bool ignoreFallen = false)
{
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!networkPlayer->m_pPed ||
            (ignoreFallen && (networkPlayer->m_bDead || networkPlayer->m_pPed->m_fHealth <= 0.0f)) ||
            !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) || networkPlayer->m_pPed->m_nAreaCode == areaCode)
            continue;
        if (!missing.empty())
            missing += ", ";
        missing += networkPlayer->GetName();
    }
    return !missing.empty();
}

static bool IsStoryMissionRunning();
static bool bEntryWindow = false;

// Recover group followers missed by the native entry/exit warp (EntryExit.cpp, 0x43F1F0).
static void ProcessAllyFollowArea()
{
    static int missionId = -1, previousArea = -1, oldArea = -1;
    static int enteredArea = -1, enteredMission = -1;
    static unsigned int changedAt = 0;
    static bool pending = false;
    static std::vector<int> moved, requested;
    CPlayerPed* player = FindPlayerPed(0);
    const int currentMission = CMissionMirror::GetRunningMissionId();
    const bool eligible = CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionAllyFollowArea &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft() &&
        !CMirrorBlips::IsPerPlayerMissionRunning() && currentMission >= 0 && IsStoryMissionRunning() &&
        player && player->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED;
    if (!eligible || missionId != currentMission)
    {
        missionId = eligible ? currentMission : -1;
        previousArea = eligible ? player->m_nAreaCode : -1;
        enteredArea = enteredMission = -1;
        pending = false;
        moved.clear();
        requested.clear();
        return;
    }
    const int area = player->m_nAreaCode;
    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    if (previousArea != area)
    {
        oldArea = previousArea;
        previousArea = area;
        changedAt = now;
        // Same gate as the native warp: CEntryExit::ms_spawnPoint (0x96A7C0), bAcceptNpcGroup (EntryExit.cpp:500).
        CEntryExit* spawnPoint = *reinterpret_cast<CEntryExit**>(0x96A7C0);
        const bool returning = area == 0 && oldArea == enteredArea && enteredMission == currentMission;
        enteredArea = enteredMission = -1;
        pending = returning || (spawnPoint && spawnPoint->m_nFlags.bAcceptNpcGroup && spawnPoint->m_nArea == area);
        CPedGroup* group = CPedGroups::GetPedsGroup(player);
        CMissionMirror::LogEventf("rule-ally-follow-area change from=%d to=%d pending=%d spawn=%d group=%d",
            oldArea, area, pending ? 1 : 0, spawnPoint ? static_cast<int>(spawnPoint->m_nArea) : -1,
            group && group->m_groupMembership.IsLeader(player) ? 1 : 0);
        moved.clear();
        requested.clear();
    }
    if (!pending || now - changedAt >= 3000)
    {
        pending = false;
        return;
    }
    CPedGroup* group = CPedGroups::GetPedsGroup(player);
    if (!group || !group->m_groupMembership.IsLeader(player))
        return;
    for (int slot = 0; slot < 7; ++slot)
    {
        CPed* ped = group->m_groupMembership.GetMember(slot);
        if (!ped || !IsPedPointerValid(ped) || !ped->IsVTableValid() || !ped->m_matrix ||
            ped->IsPlayer() || ped->m_fHealth <= 0.0f || ped->m_nPedFlags.bInVehicle)
            continue;
        const int ref = CPools::GetPedRef(ped);
        if (std::find(moved.begin(), moved.end(), ref) != moved.end())
            continue;
        if (ped->m_nAreaCode != oldArea && DistanceBetweenPoints(ped->GetPosition(), player->GetPosition()) <= 30.0f)
            continue;
        CNetworkPed* networkPed = CNetworkPedManager::GetPed(ped);
        if (networkPed && !networkPed->m_bSyncing)
        {
            if (std::find(requested.begin(), requested.end(), ref) == requested.end())
            {
                Packets::Peds::PedTakeHost packet{};
                packet.pedid = networkPed->m_pedId;
                packet.allowReturnToPreviousHost = false;
                GetPacketFactory().Send(packet);
                requested.push_back(ref);
                CMissionMirror::LogEventf("rule-ally-follow-area claim ped=%d from=%d to=%d", ref, oldArea, area);
            }
            continue; // Wait for AssignPedSyncer: the previous owner must stop sending positions first.
        }
        moved.push_back(ref);
        // In front of the player: right after a transition the door is behind him (native check: IsTeleportPointValid).
        CVector destination = player->TransformFromObjectSpace(CVector((slot % 3 - 1) * 1.5f, 2.0f + slot / 3, 0.0f));
        if (!CWorld::GetIsLineOfSightClear(player->GetPosition(), destination, true, false, false, true, false, false, false))
            destination = player->GetPosition();
        ped->Teleport(destination, false);
        ped->m_pIntelligence->FlushImmediately(false);
        // SetAreaCode is a field assignment in gta-reversed Entity.h; SDK exposes the field.
        ped->m_nAreaCode = player->m_nAreaCode;
        ped->m_pEnex = player->m_pEnex;
        if (area != 0)
        {
            enteredArea = area;
            enteredMission = currentMission;
        }
        if (networkPed)
        {
            networkPed->m_nAreaCode = player->m_nAreaCode;
            networkPed->m_vecLastPos = destination;
            networkPed->m_bHasPosError = false;
        }
        CMissionMirror::LogEventf("rule-ally-follow-area warp ped=%d from=%d to=%d", ref, oldArea, area);
    }
}

static void ProcessEntryWindow()
{
    static int missionId = -1;
    static int previousArea = -1;
    static unsigned int enteredAt = 0;
    CPlayerPed* player = FindPlayerPed(0);
    const int currentMission = CMissionMirror::GetRunningMissionId();
    const bool eligible = CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionWaitOnEntry &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CMirrorBlips::IsPerPlayerMissionRunning() && currentMission >= 0 && IsStoryMissionRunning() &&
        player && player->m_fHealth > 0.0f && CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED && !CHomeInvasion::SuppressGather(nullptr);
    if (!eligible)
    {
        missionId = -1;
        previousArea = -1;
        bEntryWindow = false;
        return;
    }

    const int area = player->m_nAreaCode;
    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    if (missionId != currentMission)
    {
        missionId = currentMission;
        previousArea = area;
        bEntryWindow = false;
    }
    if (previousArea != area)
    {
        // Native entry transitions set the player's area (gta-reversed EntryExit.cpp:407).
        bEntryWindow = area != 0;
        enteredAt = now;
        previousArea = area;
        if (bEntryWindow)
            CMissionMirror::LogEventf("rule-entry-open area=%d", area);
    }
    if (!bEntryWindow)
        return;

    std::string missing;
    if (!FindPlayersInOtherPlace(area, missing, true))
    {
        bEntryWindow = false;
        CMissionMirror::LogEventf("rule-entry-complete area=%d", area);
        return;
    }
    if (now - enteredAt < 30000)
        return;
    // the ones who are missing are walking in from just outside the door: wait for them (up to 2 minutes)
    if (now - enteredAt < 120000 && CMissionSceneWait::MissingAreComing())
        return;

    // Consume before sending: no retries while the remote area update is in flight.
    bEntryWindow = false;
    if (CConfigLoader::ms_bMissionEntryNoTeleport)
    {
        // every player uses the door himself; the hold on entry already waited for him
        CMissionMirror::LogEventf("rule-entry-timeout area=%d players=0 missing=%s no-teleport=1", area, missing.c_str());
        return;
    }
    CNetworkPlayer* players[MAX_TELEPORT_SLOTS];
    int count = 0;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (count < MAX_TELEPORT_SLOTS && ped && !networkPlayer->m_bDead && ped->m_fHealth > 0.0f &&
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) && ped->m_nAreaCode != area)
            players[count++] = networkPlayer;
    }
    CCommandTeleportPlayersToHostSafely::TeleportToHost(players, count);
    CMissionMirror::LogEventf("rule-entry-timeout area=%d players=%d missing=%s", area, count, missing.c_str());
}

// gta-reversed CharacterCommands.cpp:0102; native LocateCharCommand (0x486D80)
// collects 8 parameters and preserves them through UpdateCompareFlag (0x486FC2).
// Every shop counter uses this same 1x1x4 stopped-on-foot blob: barber chair, food, Ammu-Nation, clothes, tattoo, wardrobe.
static bool IsShopCounterScript(const CRunningScript* script)
{
    for (const char* name : {"BARB", "JFUD", "AMUNAT", "CLOTH", "TATTO", "WARROBE"})
        if (_strnicmp(script->m_szName, name, 8) == 0)
            return true;
    return false;
}

static bool FilterShopCounter(CRunningScript* script, bool state)
{
    static bool bWaitMessageShown = false;
    CPlayerPed* localPlayer = FindPlayerPed(0);
    const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
    if (!CConfigLoader::ms_bVanillaScripts || !CConfigLoader::ms_bMissionShopOneAtATime ||
        !CNetwork::m_bConnected || !state || script->m_bNotFlag || !localPlayer ||
        localPlayer->m_fHealth <= 0.0f || ScriptParams[0] != CPools::GetPedRef(localPlayer) ||
        f[4] != 1.0f || f[5] != 1.0f || f[6] != 4.0f || ScriptParams[7] == 0)
    {
        bWaitMessageShown = false;
        return state;
    }

    const CVector center(f[1], f[2], f[3]);
    if (!CCommandIsAnyNetworkPlayerInArea3D::IsAnyNetworkPlayerInArea(center, CVector(1.2f, 1.2f, 3.0f)))
    {
        bWaitMessageShown = false;
        return state;
    }

    if (!bWaitMessageShown)
    {
        bWaitMessageShown = true;
        CChat::AddMessage("{c7a6ff}[Negocio]{ffffff} Ocupado: atienden de a uno, espera tu turno");
        CMissionMirror::LogEventf("rule-shop-wait at=%.1f,%.1f,%.1f area=%d", center.x, center.y, center.z,
            localPlayer->m_nAreaCode);
    }
    return false;
}

// Remember a pool handle, never an unreferenced vehicle pointer.
static int nGuestCarRef = -1;
static CRunningScript* pGuestCarScript = nullptr;
static int nGuestCarMission = -1;

// T97: requiresHostCar scripts whose original block is known exclude only these sections (offsets from the mission's
// start, equal to the labels of scm/scripts/*.txt). Block: offset in the original main.scm, size and FNV-1a, like
// CMissionGuestActions; anything else (other main.scm, coop scripts) keeps the whole-script exclusion.
struct SHostCarRange { uint32_t from, to; };
struct SHostCarSections { const char* name; uint32_t start, length, hash; SHostCarRange ranges[2]; int count; };
static const SHostCarSections hostCarSections[] = {
    // MUSIC1_14856..15131: beach arrival scene, turns the car the host is in (T12 MUSIC1.txt:2373/2381)
    {"MUSIC1", 0x0ECBFF, 15958, 0xD17035CC, {{14856, 15131}}, 1},
    // SMOKE3_10370..16921: the train chase, stage 80@ == 2 (drive-by and distances to the train, T12 SMOKE3.txt:1535/1578)
    {"SMOKE3", 0x0DABCA, 20874, 0x03FA2446, {{10370, 16921}}, 1},
};

static const SHostCarSections* FindHostCarSections(CRunningScript* script)
{
    static int cachedMission = -2;
    static const SHostCarSections* cachedEntry = nullptr;
    static bool cachedMatch = false;
    if (!script->m_bIsMission || script->m_pBaseIP != reinterpret_cast<uint8_t*>(CTheScripts::MissionBlock))
        return nullptr;
    const int mission = CMissionMirror::GetRunningMissionId();
    for (const auto& entry : hostCarSections)
    {
        if (_strnicmp(script->m_szName, entry.name, 8) != 0)
            continue;
        if (cachedMission == mission && cachedEntry == &entry)
            return cachedMatch ? &entry : nullptr;
        uint32_t hash = 2166136261u;
        for (uint32_t i = 0; i < entry.length; ++i)
            hash = (hash ^ script->m_pBaseIP[i]) * 16777619u;
        CMissionMirror::LogEventf("rule-guest-car-sections script=%.8s match=%d", entry.name, hash == entry.hash ? 1 : 0);
        cachedMission = mission;
        cachedEntry = &entry;
        cachedMatch = hash == entry.hash;
        return cachedMatch ? &entry : nullptr;
    }
    return nullptr;
}

// T97 MUSIC1: LOCATE_CHAR_ANY_MEANS_2D($scplayer, 34@ 35@ = 526.84 -1885.19, 50 x 50) at MUSIC1_3053 starts the arrival
// scene (MUSIC1_14856: widescreen, fixed camera, MUS1_2, the host snapped to the ground). Not a marker: it started with
// the host alone and its snap gathered the guests that were still arriving.
static bool IsBeachArrival(CRunningScript* script, const SLocate& locate)
{
    return CConfigLoader::ms_bMusic1BeachArrival && _strnicmp(script->m_szName, "MUSIC1", 8) == 0 && !locate.bSphere &&
        fabsf(locate.radius - 50.0f) < 0.01f && fabsf(locate.center.x - 526.84f) < 0.5f &&
        fabsf(locate.center.y + 1885.19f) < 0.5f;
}

static void ProcessBeachArrival()
{
    if (!bBeachArrived && !nBeachArrivalTick)
        return;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && _strnicmp(script->m_szName, "MUSIC1", 8) == 0)
            return;
    }
    // a new attempt waits again
    bBeachArrived = false;
    nBeachArrivalTick = 0;
}

// T98: a vehicle the running mission script made itself (CTheScripts::MissionCleanUp, gta-reversed MissionCleanup.cpp):
// not a network copy of a guest's vehicle nor one this DLL parked (both are MISSION_VEHICLE too)
static bool IsMissionCleanupVehicle(CVehicle* vehicle)
{
    if (!vehicle)
        return false;
    const int handle = CPools::GetVehicleRef(vehicle);
    for (const auto& entry : CTheScripts::MissionCleanUp.m_Objects)
        if (entry.type == MISSION_CLEANUP_ENTITY_TYPE_VEHICLE && entry.handle == handle)
            return true;
    return false;
}

// the mission needs the host himself in its vehicle here: a guest driving it does not count
static bool IsHostCarRequired(CRunningScript* script, const uint8_t* ip)
{
    // These scripts read the host's car or need him in a chase or a continuous scene.
    static const char* const requiresHostCar[] = {"CAT4", "GARAG1", "GUNS1", "MANSIO2", "RYDER2", "SYN1", "SYN2",
        "CASINO5", "CAT2", "DRUGS3", "FINALEA", "SCRASH3", "SYN7", "SMOKE2", "SMOKE3", "DRIV3", "DRIV6",
        "DES3", "MUSIC2", "HOODS5", "INTRO1", "DRIV2", "MUSIC1"};
    for (const char* name : requiresHostCar)
    {
        if (_strnicmp(script->m_szName, name, 8) != 0)
            continue;
        const SHostCarSections* sections =
            CConfigLoader::ms_bMissionGuestCarSections && CConfigLoader::ms_bVanillaScripts ? FindHostCarSections(script) : nullptr;
        if (!sections || !ip || ip < script->m_pBaseIP || ip >= script->m_pBaseIP + sections->length)
            return true;
        const uint32_t offset = static_cast<uint32_t>(ip - script->m_pBaseIP);
        for (int i = 0; i < sections->count; ++i)
        {
            if (offset >= sections->ranges[i].from && offset < sections->ranges[i].to)
                return true;
        }
        return false;
    }
    return false;
}

static bool CanUseGuestCar(CRunningScript* script, const uint8_t* ip)
{
    if (IsHostCarRequired(script, ip))
        return false;
    CPlayerPed* player = FindPlayerPed(0);
    return CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionAnyPlayerInCar &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && player && player->m_fHealth > 0.0f &&
        CCoopMission::IsScriptAuthority() && script->m_bIsMission && COpCodeSync::IsScriptSynced(script) &&
        CMissionSyncRules::IsStoryScript(script) && !CMirrorBlips::IsPerPlayerMissionRunning();
}

static CNetworkPlayer* FindGuestInMissionCar(CVehicle* vehicle)
{
    CPlayerPed* player = FindPlayerPed(0);
    // gta-reversed Vehicle.h: MISSION_VEHICLE=2; CharacterCommands.cpp: IsCharInCar.
    if (!vehicle || vehicle->m_nCreatedBy != MISSION_VEHICLE || vehicle->m_fHealth <= 0.0f || !player ||
        vehicle->m_nAreaCode != player->m_nAreaCode)
        return nullptr;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) && ped && ped->m_fHealth > 0.0f &&
            ped->m_nAreaCode == vehicle->m_nAreaCode && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle == vehicle)
            return networkPlayer;
    }
    return nullptr;
}

static void ClearGuestCar()
{
    nGuestCarRef = -1;
    pGuestCarScript = nullptr;
    nGuestCarMission = -1;
}

static void ProcessGuestCar()
{
    if (!pGuestCarScript)
        return;
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script == pGuestCarScript && CanUseGuestCar(script, script->m_pCurrentIP) &&
            nGuestCarMission == CMissionMirror::GetRunningMissionId() &&
            FindGuestInMissionCar(CPools::GetVehicle(nGuestCarRef)))
            return;
    }
    ClearGuestCar();
}

// T98: the 2026-11-11 test had guests driving mission cars and not one rule-any-player-in-car: when a guest sits in the
// car IS_CHAR_IN_CAR / IS_CHAR_SITTING_IN_CAR asks about for the host and the rule does not take it, log why
static void LogGuestCarMiss(CRunningScript* script, uint16_t opcode)
{
    CPlayerPed* player = FindPlayerPed(0);
    CVehicle* vehicle = CPools::GetVehicle(ScriptParams[1]);
    if (!player || !vehicle || ScriptParams[0] != CPools::GetPedRef(player) || !script->m_bIsMission ||
        COpCodeSync::bProcessingNetworkOpcode)
        return;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || !ped->m_nPedFlags.bInVehicle || ped->m_pVehicle != vehicle)
            continue;
        const uint8_t* ip = CommandHooks::GetCurrentCommandIP();
        if (CanUseGuestCar(script, ip) && FindGuestInMissionCar(vehicle))
            return;
        static DWORD nLastMiss = 0;
        const DWORD now = GetTickCount();
        if (now - nLastMiss < 5000)
            return;
        nLastMiss = now;
        CMissionMirror::LogEventf("rule-guest-car-miss script=%.8s opcode=%04X ip=%d host-car=%d rules=%d any-car=%d "
            "auth=%d synced=%d story=%d created=%d veh-area=%d ped-area=%d host-area=%d include=%d hp=%.0f driver=%d",
            script->m_szName, opcode, ip && ip >= script->m_pBaseIP ? (int)(ip - script->m_pBaseIP) : -1,
            IsHostCarRequired(script, ip) ? 1 : 0, CMissionSyncRules::IsEnabled() ? 1 : 0,
            CConfigLoader::ms_bMissionAnyPlayerInCar ? 1 : 0, CCoopMission::IsScriptAuthority() ? 1 : 0,
            COpCodeSync::IsScriptSynced(script) ? 1 : 0, CMissionSyncRules::IsStoryScript(script) ? 1 : 0,
            (int)vehicle->m_nCreatedBy, (int)vehicle->m_nAreaCode, (int)ped->m_nAreaCode, (int)player->m_nAreaCode,
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ? 1 : 0, ped->m_fHealth,
            vehicle->m_pDriver == ped ? 1 : 0);
        return;
    }
    // testeo 13-11 (every mission): no guest in the asked car here. A guest seen in another car or on foot right by it
    // means his entry did not reach this game as that same car
    const CVector carPos = vehicle->GetPosition();
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || ped->m_fHealth <= 0.0f)
            continue;
        CVehicle* other = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle : nullptr;
        const CVector pos = other ? other->GetPosition() : ped->GetPosition();
        // a guest in a car of the same model anywhere: the parked guest copy (same model and colours) taken for the
        // mission car; the script asks about the original, which stays where it was
        if (other && other != vehicle && other->m_nModelIndex == vehicle->m_nModelIndex)
        {
            static DWORD nLastTwin = 0;
            const DWORD now = GetTickCount();
            if (now - nLastTwin < 5000)
                return;
            nLastTwin = now;
            CMissionMirror::LogEventf("rule-guest-car-twin script=%.8s opcode=%04X car=%d guest=%s other=%d other-created=%d "
                "driver=%d dist=%.1f", script->m_szName, opcode, ScriptParams[1], networkPlayer->GetName().c_str(),
                CPools::GetVehicleRef(other), (int)other->m_nCreatedBy, other->m_pDriver == ped ? 1 : 0,
                (pos - carPos).Magnitude());
            return;
        }
        if ((pos - carPos).Magnitude() > 8.0f)
            continue;
        static DWORD nLastNear = 0;
        const DWORD now = GetTickCount();
        if (now - nLastNear < 5000)
            return;
        nLastNear = now;
        CMissionMirror::LogEventf("rule-guest-car-near script=%.8s opcode=%04X car=%d created=%d guest=%s in-vehicle=%d "
            "other=%d other-created=%d dist=%.1f include=%d", script->m_szName, opcode, ScriptParams[1],
            (int)vehicle->m_nCreatedBy, networkPlayer->GetName().c_str(), ped->m_nPedFlags.bInVehicle ? 1 : 0,
            other ? CPools::GetVehicleRef(other) : -1, other ? (int)other->m_nCreatedBy : -1, (pos - carPos).Magnitude(),
            CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ? 1 : 0);
        return;
    }
}

// A marker of the mission on the authority's player that he has not reached yet while a guest already stands in it: the
// authority learns he is the one awaited (every story mission's markers, like the start markers of the launchers)
static void NotifyAwaitedAuthority(CRunningScript* script, uint16_t opcode)
{
    SLocate locate;
    if (opcode == 0x03EE || !CMissionHints::WaitNoticesEnabled() || !script->m_bIsMission || !CNetwork::m_bConnected ||
        COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script) ||
        !CMissionSyncRules::IsStoryScript(script) || CMirrorBlips::IsPerPlayerMissionRunning() ||
        !ReadLocate(opcode, locate) || !locate.bSphere || locate.bCarSelf)
        return;
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = networkPlayer->m_pPed;
        if (!ped || networkPlayer->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ||
            ped->m_nAreaCode != localPlayer->m_nAreaCode)
            continue;
        const CVector pos = ped->m_nPedFlags.bInVehicle && ped->m_pVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
        const float dx = pos.x - locate.center.x, dy = pos.y - locate.center.y, reach = locate.radius + 1.5f;
        if (dx * dx + dy * dy > reach * reach ||
            (locate.b3D && fabsf(pos.z - locate.center.z) > (std::max)(locate.height, 2.0f) + 2.0f))
            continue;
        CMissionHints::AuthorityAwaited(Packets::Scripts::MissionWaitNotice::MARKER);
        static DWORD nLastLog = 0;
        if (GetTickCount() - nLastLog > 5000)
        {
            nLastLog = GetTickCount();
            CMissionMirror::LogEventf("rule-host-awaited opcode=%04X at=%.1f,%.1f guest=%s", opcode, locate.center.x,
                locate.center.y, networkPlayer->GetName().c_str());
        }
        return;
    }
}

static bool FilterGuestCar(CRunningScript* script, uint16_t opcode, bool state)
{
    if (!state && (opcode == 0x00DB || opcode == 0x0448))
        LogGuestCarMiss(script, opcode);
    if (!CanUseGuestCar(script, CommandHooks::GetCurrentCommandIP()) || ScriptParams[0] != CPools::GetPedRef(FindPlayerPed(0)))
        return state;

    CVehicle* vehicle = nullptr;
    CNetworkPlayer* occupant = nullptr;
    if (opcode == 0x00DB || opcode == 0x0448)
    {
        vehicle = CPools::GetVehicle(ScriptParams[1]);
        occupant = FindGuestInMissionCar(vehicle);
        if (!state && occupant)
        {
            nGuestCarRef = ScriptParams[1];
            pGuestCarScript = script;
            nGuestCarMission = CMissionMirror::GetRunningMissionId();
        }
    }
    else
    {
        bool b3D = false, bStopped = false, bArea = false;
        bool bFixed = true;
        CEntity* target = nullptr;
        switch (opcode)
        {
        case 0x00EC:  // LOCATE_CHAR_ANY_MEANS_2D
        case 0x00EE: break;  // LOCATE_CHAR_IN_CAR_2D
        case 0x00EF:
        case 0x00F1: bStopped = true; break;
        case 0x00FE:
        case 0x0100: b3D = true; break;
        case 0x0101:
        case 0x0103: b3D = true; bStopped = true; break;
        case 0x00A3:
        case 0x01A2: bArea = true; break;
        case 0x01A3:
        case 0x01A5: bArea = true; bStopped = true; break;
        case 0x00A4:
        case 0x01A7: bArea = true; b3D = true; break;
        case 0x01A8:
        case 0x01AA: bArea = true; b3D = true; bStopped = true; break;
        case 0x00F2:
        case 0x00F4: target = CPools::GetPed(ScriptParams[1]); bFixed = false; break;
        case 0x0104:
        case 0x0106: target = CPools::GetPed(ScriptParams[1]); bFixed = false; b3D = true; break;
        case 0x0202:
        case 0x0204: target = CPools::GetVehicle(ScriptParams[1]); bFixed = false; break;
        case 0x0205:
        case 0x0207: target = CPools::GetVehicle(ScriptParams[1]); bFixed = false; b3D = true; break;
        case 0x0471:
        case 0x0473: target = CPools::GetObject(ScriptParams[1]); bFixed = false; break;
        case 0x0474:
        case 0x0476: target = CPools::GetObject(ScriptParams[1]); bFixed = false; b3D = true; break;
        default: return state;
        }
        if (state || pGuestCarScript != script || nGuestCarMission != CMissionMirror::GetRunningMissionId())
            return state;
        vehicle = CPools::GetVehicle(nGuestCarRef);
        occupant = FindGuestInMissionCar(vehicle);
        if (!occupant)
        {
            ClearGuestCar();
            return state;
        }
        // Native LOCATE uses independent inclusive axis bounds; sphere only draws the marker.
        if (!bFixed && (!target || target->m_nAreaCode != vehicle->m_nAreaCode))
            return state;
        const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
        const CVector pos = vehicle->GetPosition();
        if (bArea)
        {
            // CharInAreaCheckCommand (0x488B50) normalizes both corners, inclusively.
            const int end = b3D ? 4 : 3;
            if (!(pos.x >= (std::min)(f[1], f[end]) && pos.x <= (std::max)(f[1], f[end]) &&
                  pos.y >= (std::min)(f[2], f[end + 1]) && pos.y <= (std::max)(f[2], f[end + 1]) &&
                  (!b3D || (pos.z >= (std::min)(f[3], f[6]) && pos.z <= (std::max)(f[3], f[6])))))
                return state;
        }
        else
        {
            const CVector center = bFixed ? CVector(f[1], f[2], b3D ? f[3] : 0.0f) : target->GetPosition();
            const int radius = bFixed ? (b3D ? 4 : 3) : 2;
            if (!(pos.x >= center.x - f[radius] && pos.x <= center.x + f[radius] &&
                  pos.y >= center.y - f[radius + 1] && pos.y <= center.y + f[radius + 1] &&
                  (!b3D || (pos.z >= center.z - f[radius + 2] && pos.z <= center.z + f[radius + 2]))))
                return state;
        }
        if (bStopped && !CTheScripts::IsPedStopped(occupant->m_pPed))
            return state;
    }
    if (state || !occupant)
        return state;
    static DWORD nLastLog = 0;
    const DWORD now = GetTickCount();
    if (now - nLastLog >= 5000)
    {
        nLastLog = now;
        CMissionMirror::LogEventf("rule-any-player-in-car opcode=%04X vehicle=%d player=%s", opcode,
            CPools::GetVehicleRef(vehicle), occupant->GetName().c_str());
    }
    return true;
}

// BCRASH1 (coop-badlands): the witness (284@) is watched and chased by every mission player, not only the host.
// IS_LINE_OF_SIGHT_CLEAR from the host's coords (15/30 s out of sight = lost), LOCATE_CHAR_ANY_MEANS_CHAR_3D
// host-witness 100/25 m (lost), _2D 10 m (he flees) and LOCATE_CHAR_ANY_MEANS_3D 100x100x30 around his body (gone
// without the photo). The 4-5 m locates stay host-only. Raw results, before the native NOT.
static bool FilterBadlands(uint16_t opcode)
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
static bool FilterCountryside(CRunningScript* script, uint16_t opcode)
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

static bool __cdecl FilterCompareFlag(CRunningScript* script, bool state)
{
    if (!script || CommandHooks::GetCurrentScript() != script)
        return state;

    const uint16_t opcode = CommandHooks::GetCurrentCommand() & 0x7FFF;
    bool crateHandled = false;
    const bool crateState = CHomeInvasion::FilterCompare(script, opcode, state, crateHandled);
    if (crateHandled) return crateState;
    bool actionHandled = false;
    const bool actionState = CMissionGuestActions::FilterCompare(script, opcode, state, actionHandled);
    if (actionHandled) return actionState;
    state = CActivityChallenge::FilterCompare(script, opcode, state);
    state = CPatientHandover::FilterCompare(script, opcode, state);
    state = CCoopLowrider::FilterDanceResult(script, opcode, state);
    // FilterStart only ever turns a true into false: with a false state it returned false too and this cut every rule
    // below that turns a false into true (guest at the wheel, guest in the marker, Badlands, countryside...)
    if (state && !CHighStakesRace::FilterStart(script, opcode, state)) return false;
    if (CCoopPool::Enabled())
        state = CCoopPool::FilterCompare(script, opcode, state);
    // T63 has an explicit allowlist; its spots must not enter the all-player wait below.
    bool guestHandled = false;
    const bool guestState = CMissionGuestCounts::FilterCompare(script, opcode, state, guestHandled);
    if (guestHandled || guestState != state)
        return guestState;
    // shop scripts run locally, outside mission authority and synchronization rules.
    if (opcode == 0x0102 && IsShopCounterScript(script) && !COpCodeSync::bProcessingNetworkOpcode)
        return FilterShopCounter(script, state);

    if (!CMissionSyncRules::IsEnabled())
        return state;

    // Sample before false comparisons return: onmission can prevent reaching 03EE.
    if (CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && IsStoryLauncher(script))
    {
        static std::unordered_map<std::string, DWORD> lastLaunchLog;
        const std::string name(script->m_szName, strnlen(script->m_szName, 8));
        const DWORD now = GetTickCount();
        const auto last = lastLaunchLog.find(name);
        if (last == lastLaunchLog.end() || now - last->second >= 5000)
        {
            lastLaunchLog[name] = now;
            const int counter = _strnicmp(script->m_szName, "SWEET", 8) == 0
                ? *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808]) : -1;
            const int onmission = CTheScripts::OnAMissionFlag
                ? *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]) : -1;
            CMissionMirror::LogEventf("solo-launch script=%.8s counter=%d onmission=%d authority=%d individual=%d opcode=%04X state=%d",
                script->m_szName, counter, onmission, CCoopMission::IsScriptAuthority() ? 1 : 0,
                CMissionMode::IsIndividual() ? 1 : 0, opcode, state ? 1 : 0);
        }
    }
    if (!state && opcode >= 0x02CA && opcode <= 0x02CC && CConfigLoader::ms_bMissionSharedOnScreen && script &&
        script->m_bIsMission && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script) &&
        CMissionSyncRules::IsStoryScript(script))
    {
        // gta-reversed eScriptCommands.h: IS_CAR/CHAR/OBJECT_ON_SCREEN, before the native NOT.
        CEntity* entity = nullptr;
        switch (opcode)
        {
        case 0x02CA: entity = CPools::GetVehicle(ScriptParams[0]); break;
        case 0x02CB: entity = CPools::GetPed(ScriptParams[0]); break;
        case 0x02CC: entity = CPools::GetObject(ScriptParams[0]); break;
        }
        if (entity)
        {
            const bool wideCar = opcode == 0x02CA && CConfigLoader::ms_bMissionWideCarOnScreen &&
                static_cast<CVehicle*>(entity)->m_nCreatedBy == MISSION_VEHICLE;
            const float radius = wideCar ? ON_SCREEN_SHARED_CAR_RADIUS : ON_SCREEN_SHARED_RADIUS;
            for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) || !networkPlayer->m_pPed ||
                    networkPlayer->m_pPed->m_nAreaCode != entity->m_nAreaCode)
                    continue;
                const CVector delta = networkPlayer->m_pPed->GetPosition() - entity->GetPosition();
                if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >= radius * radius)
                    continue;

                static DWORD nLastOnScreenLog = 0;
                const DWORD now = GetTickCount();
                if (now - nLastOnScreenLog >= 5000)
                {
                    nLastOnScreenLog = now;
                    CMissionMirror::LogEventf("rule-on-screen opcode=%04X near=%s", opcode, networkPlayer->GetName().c_str());
                }
                return true;
            }
        }
    }
    if (!state && opcode == 0x00FE && CConfigLoader::ms_bMissionAnyPlayerTrigger &&
        script->m_bIsMission && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        struct SAnyPlayerTrigger
        {
            const char* script;
            CVector center;
            CVector radius;
        };
        static const SAnyPlayerTrigger triggers[] = {
            {"HOODS5", {2775.793f, -1942.909f, 15.6576f}, {100.0f, 100.0f, 50.0f}}
        };
        CPlayerPed* localPlayer = FindPlayerPed(0);
        const float* f = reinterpret_cast<const float*>(&ScriptParams[0]);
        for (const auto& trigger : triggers)
        {
            if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer) ||
                _strnicmp(script->m_szName, trigger.script, 8) != 0 ||
                !(fabsf(f[1] - trigger.center.x) <= 0.1f && fabsf(f[2] - trigger.center.y) <= 0.1f &&
                  fabsf(f[3] - trigger.center.z) <= 0.1f && fabsf(f[4] - trigger.radius.x) <= 0.1f &&
                  fabsf(f[5] - trigger.radius.y) <= 0.1f && fabsf(f[6] - trigger.radius.z) <= 0.1f))
                continue;
            for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                CPed* ped = networkPlayer->m_pPed;
                if (!ped || networkPlayer->m_bDead || !(ped->m_fHealth > 0.0f) ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer) ||
                    ped->m_nAreaCode != localPlayer->m_nAreaCode)
                    continue;
                if (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)
                    continue;
                // Native GetCharCoordinates uses the vehicle position; CBox bounds are inclusive.
                const CVector pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (!(pos.x >= f[1] - f[4] && pos.x <= f[1] + f[4] &&
                      pos.y >= f[2] - f[5] && pos.y <= f[2] + f[5] &&
                      pos.z >= f[3] - f[6] && pos.z <= f[3] + f[6]))
                    continue;
                static DWORD nLastTriggerLog = 0;
                const DWORD now = GetTickCount();
                if (now - nLastTriggerLog >= 5000)
                {
                    nLastTriggerLog = now;
                    CMissionMirror::LogEventf("rule-any-player-trigger script=%.8s player=%s",
                        script->m_szName, networkPlayer->GetName().c_str());
                }
                return true;
            }
        }
    }
    // gta-reversed LocateCharAnyMeansChar2D: rectangular bounds, before native NOT.
    // Only the 300 m escape test; Smoke's small dialogue trigger stays host-only.
    if (!state && opcode == 0x00F2 && CConfigLoader::ms_bCoopRunningDog &&
        CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        !_strnicmp(script->m_szName, "SMOKE2", 8) && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        auto* target = CPools::GetPed(ScriptParams[1]);
        if (host && target && ScriptParams[0] == CPools::GetPedRef(host) &&
            f[2] == 300.0f && f[3] == 300.0f && ScriptParams[4] == 0)
        {
            const auto center = target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                    ped->m_nAreaCode != target->m_nAreaCode ||
                    (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3])
                    return true;
            }
        }
    }
    // DRUGS3 boss chase (coop-gray-imports): LOCATE_CHAR_ANY_MEANS_CHAR_2D between the host and the boss, either order.
    // 200 m = lost him (fail), 60 m = caught up, 40 m = his recording's pace: any mission player near counts.
    // TWAR7 (coop-og-loc-escape): Freddy escapes when the host is past 250 m. The 3-35 m triggers stay host-only.
    const bool grayImports = CConfigLoader::ms_bCoopGrayImports && !_strnicmp(script->m_szName, "DRUGS3", 8);
    const bool ogLocEscape = CConfigLoader::ms_bCoopOgLocEscape && !_strnicmp(script->m_szName, "TWAR7", 8);
    if (!state && opcode == 0x00F2 && (grayImports || ogLocEscape) && CConfigLoader::ms_bVanillaScripts &&
        script->m_bIsMission && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        const int hostRef = host ? CPools::GetPedRef(host) : -1;
        const bool chaseSize = f[2] == f[3] &&
            (grayImports ? (f[2] == 200.0f || f[2] == 60.0f || f[2] == 40.0f) : f[2] == 250.0f);
        CPed* target = nullptr;
        if (host && chaseSize && ScriptParams[4] == 0)
        {
            if (ScriptParams[0] == hostRef) target = CPools::GetPed(ScriptParams[1]);
            else if (ScriptParams[1] == hostRef) target = CPools::GetPed(ScriptParams[0]);
        }
        if (target && !target->IsPlayer())
        {
            const auto center = target->m_nPedFlags.bInVehicle && target->m_pVehicle ? target->m_pVehicle->GetPosition()
                                                                                       : target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                    ped->m_nAreaCode != target->m_nAreaCode || (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3])
                {
                    if (f[2] >= 200.0f)
                        CMissionMirror::LogEventf("rule-chase-near script=%.8s player=%s", script->m_szName,
                            guest->GetName().c_str());
                    return true;
                }
            }
        }
    }
    if (!state && CConfigLoader::ms_bCoopBadlands && CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        !_strnicmp(script->m_szName, "BCRASH1", 8) && CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script) && FilterBadlands(opcode))
        return true;
    if (!state && CConfigLoader::ms_bCoopCountryside && CConfigLoader::ms_bVanillaScripts && script->m_bIsMission &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        COpCodeSync::IsScriptSynced(script) && FilterCountryside(script, opcode))
        return true;
    // gta-reversed LocateCharEntity2D/LocateChar2D: inclusive rectangle, before NOT.
    // Preserve the train speed/traffic tests (20-50 m); only extend escape checks.
    if (!state && CConfigLoader::ms_bCoopWrongSideTracks && CConfigLoader::ms_bVanillaScripts &&
        script->m_bIsMission && !_strnicmp(script->m_szName, "SMOKE3", 8) &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode &&
        CCoopMission::IsScriptAuthority() && COpCodeSync::IsScriptSynced(script))
    {
        const float* f = reinterpret_cast<const float*>(ScriptParams);
        auto* host = FindPlayerPed(0);
        CEntity* target = nullptr;
        if (opcode == 0x0202 && ((f[2] == 200.0f && f[3] == 200.0f) ||
            (f[2] == 320.0f && f[3] == 320.0f)))
        {
            auto* vehicle = CPools::GetVehicle(ScriptParams[1]);
            if (vehicle && vehicle->m_nVehicleSubType == VEHICLE_TRAIN) target = vehicle;
        }
        else if (opcode == 0x00F2 && f[2] == 50.0f && f[3] == 50.0f)
        {
            auto* ped = CPools::GetPed(ScriptParams[1]);
            if (ped && ped->m_nCreatedBy == 2 && !ped->IsPlayer() &&
                !CNetworkPlayerManager::GetPlayer(ped)) target = ped;
        }
        if (target && host && ScriptParams[0] == CPools::GetPedRef(host) && ScriptParams[4] == 0)
        {
            const auto center = target->GetPosition();
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                auto* ped = guest->m_pPed;
                if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f ||
                    !CCoopMission::ShouldIncludeNetworkPlayer(guest) || ped->m_nAreaCode != target->m_nAreaCode ||
                    (ped->m_nPedFlags.bInVehicle && !ped->m_pVehicle)) continue;
                const auto pos = ped->m_nPedFlags.bInVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (fabsf(pos.x - center.x) <= f[2] && fabsf(pos.y - center.y) <= f[3]) return true;
            }
        }
    }
    const bool guestCarState = FilterGuestCar(script, opcode, state);
    const bool bGuestCarOverride = !state && guestCarState;
    state = guestCarState;
    if (!state)
    {
        NotifyAwaitedAuthority(script, opcode);
        return state;
    }
    if (opcode == 0x03EE)
        return (CNetwork::m_bConnected && !script->m_bNotFlag && IsStoryLauncher(script)) ? FilterMissionStart(script)
                                                                                              : state;

    // Object LOCATEs had no wait rule before this feature.
    if ((opcode == 0x0471 || opcode == 0x0473 || opcode == 0x0474 || opcode == 0x0476) && !bGuestCarOverride)
        return state;
    SLocate locate;
    if (!ReadLocate(opcode, locate) || !script->m_bIsMission || !CNetwork::m_bConnected ||
        COpCodeSync::bProcessingNetworkOpcode ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script) ||
        !CMissionSyncRules::IsStoryScript(script) || CMirrorBlips::IsPerPlayerMissionRunning())
        return state;

    // Just Business only (the user, 2026-11-12): no marker of the mission waits for the others, it bugs the mission.
    // The start of the mission (03EE) and the entry window teleport keep their rules.
    if (CConfigLoader::ms_bJustBusinessNoMarkerWait && CConfigLoader::ms_bCoopJustBusiness &&
        CConfigLoader::ms_bVanillaScripts && !_strnicmp(script->m_szName, "DRUGS1", 8))
        return state;

    // on the authority's player, or on the vehicle he is in
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer)
        return state;
    const bool bGuestDrivenCar = locate.bCarSelf && CConfigLoader::ms_bMissionAnyPlayerInCar &&
        FindGuestInMissionCar(CPools::GetVehicle(ScriptParams[0]));
    if (locate.bCarSelf)
    {
        const bool bLocalCar = localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle &&
            ScriptParams[0] == CPools::GetVehicleRef(localPlayer->m_pVehicle);
        // Keep marker waits when the authority is on foot and a guest drives the remembered car.
        const bool bGuestCar = pGuestCarScript == script && CanUseGuestCar(script, CommandHooks::GetCurrentCommandIP()) &&
            nGuestCarMission == CMissionMirror::GetRunningMissionId() && ScriptParams[0] == nGuestCarRef &&
            FindGuestInMissionCar(CPools::GetVehicle(nGuestCarRef));
        if (!bLocalCar && !bGuestCar && !bGuestDrivenCar)
            return state;
    }
    else if (ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return state;

    // A guest's car reaching a small LOCATE must not leave the authority behind.
    if ((bGuestCarOverride || bGuestDrivenCar) && locate.radius <= 20.0f && localPlayer->m_fHealth > 0.0f &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED)
    {
        CVehicle* vehicle = CPools::GetVehicle(bGuestDrivenCar ? ScriptParams[0] : nGuestCarRef);
        // 12 m for every mission (the user, 2026-11-12: T98's 50 m in the LS missions is gone)
        if (vehicle && !IsPedNearMissionLocate(localPlayer, locate.center, locate.radius, locate.height,
                locate.b3D, vehicle->m_nAreaCode, 12.0f))
        {
            static DWORD nLastHostMessage = 0;
            const DWORD now = GetTickCount();
            if (now - nLastHostMessage >= 5000)
            {
                nLastHostMessage = now;
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Acercate al marcador para seguir la mision");
                CMissionMirror::LogEventf("rule-wait-host-guest-car opcode=%04X at=%.1f,%.1f", opcode,
                    locate.center.x, locate.center.y);
            }
            return false;
        }
    }

    const bool driveBy = CConfigLoader::ms_bCoopDriveBy && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SWEET4", 8);
    const bool sepulcros = CConfigLoader::ms_bCoopLosSepulcros && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SWEET7", 8);
    const bool runningDog = CConfigLoader::ms_bCoopRunningDog && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SMOKE2", 8);
    const bool tracks = CConfigLoader::ms_bCoopWrongSideTracks && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "SMOKE3", 8);
    const bool business = CConfigLoader::ms_bCoopJustBusiness && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(script->m_szName, "DRUGS1", 8);
    // The spray-shop checks guide the authority's car, not a convoy inside one garage.
    if (driveBy && locate.bFixedPlace && locate.b3D && locate.radius <= 20.0f &&
        ((DistanceBetweenPoints(locate.center, CVector(2075.55f, -1831.09f, 12.21f)) < 0.2f) ||
         (DistanceBetweenPoints(locate.center, CVector(1024.55f, -1039.09f, 35.21f)) < 0.2f))) return state;
    if (opcode == 0x00EC && !script->m_bNotFlag && IsBeachArrival(script, locate))
    {
        // the arrival scene waits for the players of the mission (60 m around the party; this 50 x 50 box plus a margin)
        std::string waiting;
        if (bBeachArrived || AreMissionPlayersNear(locate.center, 20.0f, 0.0f, false, waiting, 60.0f))
        {
            if (!bBeachArrived)
            {
                bBeachArrived = true;
                nBeachArrivalTick = GetTickCount();
                CMissionMirror::LogEvent("rule-beach-arrival");
            }
            return state;
        }
        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::BEACH, waiting);
        static DWORD nLastBeachMessage = 0;
        if (GetTickCount() - nLastBeachMessage > 5000)
        {
            nLastBeachMessage = GetTickCount();
            if (!CMissionHints::WaitNoticesEnabled())
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s para seguir la mision.", waiting.c_str());
            CMissionMirror::LogEventf("rule-wait-beach missing=%s", waiting.c_str());
        }
        return false;
    }

    std::string missing;
    if (locate.bSphere || (bGuestDrivenCar && locate.radius <= 20.0f))
    {
        // a marker of the mission (red cylinder, also for a car or around a char): everybody of the mission in it, the
        // NOT form included (`not locate` loops until the player arrives)
        if (AreMissionPlayersNear(locate.center, locate.radius, locate.height, locate.b3D, missing, 4.0f))  // 4 m for every mission, nobody teleported in (the user, 2026-11-12: 12 m pulled the other in)
            return state;

        CMissionHints::AuthorityWait(Packets::Scripts::MissionWaitNotice::MARKER, missing);
        static DWORD nLastMessage = 0;
        if (GetTickCount() - nLastMessage > 5000)
        {
            nLastMessage = GetTickCount();
            if (!CMissionHints::WaitNoticesEnabled())
                CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a %s para seguir la mision.", missing.c_str());
            CMissionMirror::LogEventf("rule-wait opcode=%04X at=%.1f,%.1f missing=%s", opcode, locate.center.x,
                locate.center.y, missing.c_str());
        }
        return false;
    }

    // Process also runs after scripts; catch entry before a same-frame scene LOCATE.
    ProcessEntryWindow();

    // no marker: a small fixed place the authority reached while players of the mission are in another place (still
    // inside the barber when he walked out of it, still outside the pizza when he bought the food, both in Ryder); the
    // scene it starts waits until they are where he is. Inside an interior only the "stand still here" locates
    // (LOCATE_STOPPED_CHAR_*, gta-reversed eScriptCommands.h): the other ones there drive fights (CASIN10, FINALEA,
    // DRUGS4), where a false result would start or skip an attack. The short entry window also admits moving LOCATEs.
    const bool bOutside = localPlayer->m_nAreaCode == 0;
    const bool bStopped = (opcode >= 0x00EF && opcode <= 0x00F1) || (opcode >= 0x0101 && opcode <= 0x0103);
    if (!locate.bFixedPlace || locate.radius > 20.0f ||
        (!bOutside && !(CConfigLoader::ms_bMissionWaitSamePlace && bStopped) && !bEntryWindow) ||
        !FindPlayersInOtherPlace(localPlayer->m_nAreaCode, missing, CConfigLoader::ms_bMissionWaitOnEntry))
        return state;

    CMissionHints::AuthorityWait(bOutside ? Packets::Scripts::MissionWaitNotice::LEAVE_INTERIOR
                                          : Packets::Scripts::MissionWaitNotice::ENTER_INTERIOR, missing);
    static DWORD nLastInteriorMessage = 0;
    if (GetTickCount() - nLastInteriorMessage > 5000)
    {
        nLastInteriorMessage = GetTickCount();
        if (CMissionHints::WaitNoticesEnabled())
        {
            // the help box of every player of the mission (AuthorityWait above)
        }
        else if (bOutside)
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a que %s salga para seguir la mision.", missing.c_str());
        else
            CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} Esperando a que %s entre para seguir la mision.", missing.c_str());
        CMissionMirror::LogEventf("%s opcode=%04X at=%.1f,%.1f area=%d missing=%s",
            bOutside ? "rule-wait-interior" : "rule-wait-place", opcode, locate.center.x, locate.center.y,
            localPlayer->m_nAreaCode, missing.c_str());
    }
    return false;
}

static void __declspec(naked) CRunningScript__UpdateCompareFlag_Hook()
{
    __asm
    {
        push ecx
        push dword ptr [esp + 8]
        push ecx
        call FilterCompareFlag
        add esp, 8
        pop ecx
        mov byte ptr [esp + 4], al

        // the instruction the jump replaced, then the rest of the function
        mov al, byte ptr [ecx + 0xD2]
        push 0x4859D6
        ret
    }
}

namespace { bool __cdecl GenericSave_OwnProgress(int arg); }  // the guest launchers, below

void CMissionSyncRules::InjectHooks()
{
    // mov al, [ecx + 0xD2]: only patched while the 1.0 US bytes are there
    const uint8_t expected[] = {0x8A, 0x81, 0xD2, 0x00, 0x00, 0x00};
    if (memcmp(reinterpret_cast<const void*>(0x4859D0), expected, sizeof(expected)) == 0)
    {
        patch::RedirectJump(0x4859D0, CRunningScript__UpdateCompareFlag_Hook);
        patch::Nop(0x4859D5, 1);
    }
    // the save of C_PcSave::SaveSlot: call GenericSave (E8 to 0x5D13E0), only with those bytes there
    const uint8_t saveCall[] = {0xE8, 0x5A, 0x83, 0xFB, 0xFF};
    if (memcmp(reinterpret_cast<const void*>(0x619081), saveCall, sizeof(saveCall)) == 0)
        patch::RedirectCall(0x619081, GenericSave_OwnProgress);
}

bool CMissionSyncRules::IsEnabled()
{
    return CConfigLoader::ms_bMissionSyncRules;
}

bool CMissionSyncRules::IsStoryScript(CRunningScript* pScript)
{
    if (pScript == nullptr)
        return false;
    if (CCoopPool::Enabled() && CCoopPool::IsPool(pScript)) return false;
    if (_strnicmp(pScript->m_szName, "SWEET6", 8) == 0 &&
        (!CConfigLoader::ms_bCoopLowrider || !CConfigLoader::ms_bVanillaScripts))
        return false;
    for (const char* szName : aszSyncedScripts)
    {
        if (_strnicmp(pScript->m_szName, szName, 7) == 0)
            return true;
    }
    return false;
}

bool CMissionSyncRules::MutesTeleportCommand(CRunningScript* pScript)
{
    return IsEnabled() && IsStoryScript(pScript) && !CMirrorBlips::IsPerPlayerMissionRunning();
}

bool CMissionSyncRules::MutesCheckpointCommands(CRunningScript* pScript)
{
    return IsEnabled() && IsStoryScript(pScript) && CConfigLoader::ms_bNativeMarkers && CMirrorBlips::MutesCoopBlipCommands();
}

static void OnPlayerEquipment(CRunningScript* pScript, uint16_t opcode)
{
    if (!CMissionSyncRules::IsEnabled() || !CConfigLoader::ms_bMissionPlayerEquipment || !pScript ||
        !pScript->m_bIsMission || COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript) ||
        !CMissionSyncRules::IsStoryScript(pScript) || CMirrorBlips::IsPerPlayerMissionRunning())
        return;

    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return;

    const int weaponType = ScriptParams[1];
    int ammo = (opcode == 0x01B2 || opcode == 0x017B) ? ScriptParams[2] : 0;
    if (weaponType < 0 || weaponType > 46 || (weaponType >= 19 && weaponType <= 21) || ammo < 0)
        return;

    // SWEET2 restores saved ammo + 60 here; keep the 30000 practice refill intact.
    if (CConfigLoader::ms_bMissionNinesAmmoRepair && opcode == 0x017B && weaponType == 22 &&
        ammo >= 9000 && ammo != 30000 && _strnicmp(pScript->m_szName, "SWEET2", 8) == 0)
    {
        const auto type = static_cast<eWeaponType>(weaponType);
        const auto info = CWeaponInfo::GetWeaponInfo(type, 1);
        if (!info || localPlayer->m_aWeapons[info->m_nSlot].m_eWeaponType != type)
            return;
        localPlayer->SetAmmo(type, 60);
        CMissionMirror::LogEventf("rule-ammo-repair from=%d to=60", ammo);
        ammo = 60;
    }

    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;
        Packets::Scripts::MissionPlayerEquipment packet{};
        packet.playerid = networkPlayer->m_playerId;
        packet.weaponType = static_cast<uint8_t>(weaponType);
        packet.ammo = ammo;
        packet.equip = opcode == 0x01B9;
        packet.remove = opcode == 0x0555;
        packet.setAmmo = opcode == 0x017B;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("rule-equipment player=%u weapon=%d ammo=%d equip=%d remove=%d setAmmo=%d", packet.playerid.value(),
            weaponType, ammo, packet.equip ? 1 : 0, packet.remove ? 1 : 0, packet.setAmmo ? 1 : 0);
    }
}

static void OnScriptedExit(CRunningScript* pScript, uint16_t opcode)
{
    if (!CMissionSyncRules::IsEnabled() || !CConfigLoader::ms_bMissionScriptedExit || !pScript ||
        !pScript->m_bIsMission || COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript) ||
        !CMissionSyncRules::IsStoryScript(pScript))
        return;

    CPlayerPed* player = FindPlayerPed(0);
    if (!player || ScriptParams[0] != CPools::GetPedRef(player))
        return;

    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
            continue;
        Packets::Scripts::MissionScriptedExit packet{};
        packet.playerid = networkPlayer->m_playerId;
        packet.opcode = opcode;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("rule-scripted-exit opcode=%04X player=%u", opcode, packet.playerid.value());
    }
}

// TWAR7 (coop-og-loc-speed): Freddy's recording runs faster the nearer the chaser is (x2 under 20 m, x0.5 past 110 m).
// The original measures the host only; the mission's nearest player sets it, as the coop script of this mod did.
static void OnOgLocPlaybackSpeed(CRunningScript* script)
{
    if (!CConfigLoader::ms_bCoopOgLocSpeed || !CConfigLoader::ms_bVanillaScripts || !script || !script->m_bIsMission ||
        _strnicmp(script->m_szName, "TWAR7", 8) || COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script)) return;
    CVehicle* bike = CPools::GetVehicle(ScriptParams[0]);
    CPlayerPed* host = FindPlayerPed(0);
    if (!bike || !host) return;
    const CVector at = bike->GetPosition();
    auto distance2D = [&at](const CVector& pos) { return sqrtf((pos.x - at.x) * (pos.x - at.x) + (pos.y - at.y) * (pos.y - at.y)); };
    const float hostDistance = distance2D(host->GetPosition());
    float nearest = hostDistance;
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        auto* ped = guest->m_pPed;
        if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
            ped->m_nAreaCode != bike->m_nAreaCode) continue;
        nearest = std::min(nearest, distance2D(ped->GetPosition()));
    }
    if (nearest >= hostDistance) return;
    // the script's own curve (TWAR7: 2 - d/20 under 21 m, else 1 - (d-20)/90; kept within 0.5..2)
    float speed = nearest < 21.0f ? 2.0f - nearest / 20.0f : 1.0f - (nearest - 20.0f) / 90.0f;
    speed = std::clamp(speed, 0.5f, 2.0f);
    // gta-reversed VehicleRecording.h: pVehicleForPlayback 0x97D840 / PlaybackSpeed 0x97D700, 16 slots
    auto vehicles = reinterpret_cast<CVehicle**>(0x97D840);
    auto speeds = reinterpret_cast<float*>(0x97D700);
    for (int i = 0; i < 16; i++)
        if (vehicles[i] == bike) speeds[i] = speed;
}

// MUSIC3 (coop-management-issues): the Elegant must roll off the pier with nobody inside; a guest still seated would
// hold the script's "not in the car" (any player counts there) and ride it into the water. He is asked out.
static void ProcessManagementPier()
{
    static DWORD lastAsk = 0;
    if (!CConfigLoader::ms_bCoopManagementIssues || !CConfigLoader::ms_bVanillaScripts || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || CMissionMirror::GetRunningMissionId() != 33 ||
        GetTickCount() - lastAsk < 1500) return;
    CPlayerPed* host = FindPlayerPed(0);
    if (!host || host->m_nPedFlags.bInVehicle) return;
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        auto* ped = guest->m_pPed;
        if (!ped || !ped->m_nPedFlags.bInVehicle || !ped->m_pVehicle || !CCoopMission::ShouldIncludeNetworkPlayer(guest)) continue;
        CVehicle* car = ped->m_pVehicle;
        const CVector pos = car->GetPosition();
        // the pier box of the script (855.6,-2067.9 .. 812.6,-2085.9) a few metres wider
        if (car->m_nModelIndex != 507 || car->m_nCreatedBy != MISSION_VEHICLE || pos.x < 808.0f || pos.x > 860.0f ||
            pos.y < -2090.0f || pos.y > -2040.0f) continue;
        lastAsk = GetTickCount();
        Packets::Scripts::MissionScriptedExit packet{};
        packet.playerid = guest->m_playerId;
        packet.opcode = 0x0633;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("rule-management-pier-exit player=%u", packet.playerid.value());
    }
}

static void OnCarBlipAdded(CRunningScript* pScript);
static void OnMusic1GarageOpen(CRunningScript* script);
static void OnOgLocBikeHeadingSet(CRunningScript* pScript);

static void OnJustBusinessBikeHeadingSet(CRunningScript* script)
{
    static bool creating = false;
    if (creating) return;
    if (!CConfigLoader::ms_bCoopJustBusiness || !CConfigLoader::ms_bVanillaScripts ||
        !script || !script->m_bIsMission || _strnicmp(script->m_szName, "DRUGS1", 8) ||
        COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script)) return;
    auto* bike = CPools::GetVehicle(ScriptParams[0]);
    if (!bike || bike->m_nModelIndex != 581 || bike->m_nCreatedBy != MISSION_VEHICLE ||
        bike->m_fHealth <= 0.0f ||
        DistanceBetweenPoints(bike->GetPosition(), CVector(1671.791f, -1695.106f, 19.48f)) > 3.0f ||
        fabsf(bike->GetHeading() - 117.5347f * 0.01745329252f) > 0.01f) return;
    // Escort (just-business-bike-escort): the guests ride behind Grove Street riders that follow Smoke's recordings a few
    // seconds later (CJustBusinessEscort), instead of a bike each to drive
    if (CConfigLoader::ms_bJustBusinessBikeEscort)
    {
        CJustBusinessEscort::OnSmokeBike(script, bike);
        return;
    }
    // Park before playback starts; the existing queue waits for CJ to board Smoke's bike.
    creating = true;
    // T98: bikes of two, parked with their blip; each guest gets on by himself
    if (CConfigLoader::ms_bMissionGuestVehiclesSelfBoard)
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(bike, script, "just-business", 2, true,
            CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY);
    else
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(bike, script, "just-business", 1, true);
    creating = false;
}

// MUSIC2 (coop-madd-dogg): the escape bike (510) is created after the book; each guest gets one parked next to it
static void OnMaddDoggBikeHeadingSet(CRunningScript* script)
{
    if (!CConfigLoader::ms_bCoopMaddDogg || !CConfigLoader::ms_bVanillaScripts || !script || !script->m_bIsMission ||
        _strnicmp(script->m_szName, "MUSIC2", 8) || COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script)) return;
    auto* bike = CPools::GetVehicle(ScriptParams[0]);
    if (!bike || bike->m_nModelIndex != 510 || bike->m_nCreatedBy != MISSION_VEHICLE ||
        DistanceBetweenPoints(bike->GetPosition(), CVector(1306.718f, -797.24f, 83.66f)) > 3.0f) return;
    // once per vehicle (a retry makes a new one)
    static int lastRef = -1;
    if (lastRef == ScriptParams[0])
        return;
    lastRef = ScriptParams[0];
    // a bicycle has one seat: one parked bike per guest, with its blip; each one gets on by himself
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(bike, script, "madd-dogg", 1, false,
        CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY
                                                          : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
}

// MUSIC3 (coop-management-issues): during the cutscene at the band the script seats the two managers in the back of
// the Elegant (73@) and then locks it. The guests take its free front seat, the rest one shared 4-door car next to it.
static void OnManagementPassengerWarp(CRunningScript* script)
{
    if (!CConfigLoader::ms_bCoopManagementIssues || !CConfigLoader::ms_bVanillaScripts || !script ||
        !script->m_bIsMission || _strnicmp(script->m_szName, "MUSIC3", 8) || COpCodeSync::bProcessingNetworkOpcode ||
        !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script) ||
        ScriptParams[2] != 2) return;
    auto* car = CPools::GetVehicle(ScriptParams[1]);
    if (!car || car->m_nModelIndex != 507 || car->m_nCreatedBy != MISSION_VEHICLE || car->m_fHealth <= 0.0f ||
        DistanceBetweenPoints(car->GetPosition(), CVector(1021.0f, -1122.0f, 23.0f)) > 150.0f) return;
    // once per vehicle (a retry makes a new one)
    static int lastRef = -1;
    if (lastRef == ScriptParams[1])
        return;
    lastRef = ScriptParams[1];
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, script, "management-issues", 1);
}

static bool bDriveThruGuestCarTriggered = false;
static bool bOgLocGuestCarTriggered = false;
static bool bOgLocBikesTriggered = false;
static int ogLocCarRef = -1;
static CRunningScript* ogLocCarScript = nullptr;
static DWORD ogLocCarSince = 0;
static DWORD ogLocHeadingSince = 0;
static float ogLocHeading = 0.0f;

static void ProcessOgLocGuestCar()
{
    if (ogLocCarRef == -1 || COpCodeSync::bProcessingNetworkOpcode) return;
    bool active = false;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script == ogLocCarScript && script->m_bIsMission && COpCodeSync::IsScriptSynced(script)) active = true;
    CVehicle* vehicle = CPools::GetVehicle(ogLocCarRef);
    if (!active || !vehicle || !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() ||
        !CConfigLoader::ms_bMissionSyncRules || !CConfigLoader::ms_bMissionOgLocGuestCar ||
        GetTickCount() - ogLocCarSince >= 8000)
    {
        ogLocCarRef = -1;
        return;
    }
    if (fabsf(vehicle->GetHeading() - ogLocHeading) > 0.001f)
    {
        ogLocHeading = vehicle->GetHeading();
        ogLocHeadingSince = GetTickCount();
    }
    if (GetTickCount() - ogLocHeadingSince < 100) return;
    // mission-guest-car-board: the guests are put inside (one drives, the rest ride along), not left to walk to it
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(vehicle, ogLocCarScript, "og-loc-car", 3, false,
        CConfigLoader::ms_bMissionGuestCarBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_BOARD
                                                : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
    ogLocCarRef = -1;
}

static void OnOgLocPassengerWarp(CRunningScript* script)
{
    if (bOgLocGuestCarTriggered || !CConfigLoader::ms_bMissionOgLocGuestCar || !script ||
        !script->m_bIsMission || _strnicmp(script->m_szName, "TWAR7", 8) != 0 ||
        !CConfigLoader::ms_bVanillaScripts || COpCodeSync::bProcessingNetworkOpcode ||
        !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script)) return;
    CPed* ped = CPools::GetPed(ScriptParams[0]);
    CVehicle* vehicle = CPools::GetVehicle(ScriptParams[1]);
    // OGLOC is special character slot 3 in both prison paths (normal and skip).
    if (!ped || ped->m_nModelIndex != 292 || !vehicle || vehicle->m_nModelIndex != 466 ||
        vehicle->m_nCreatedBy != MISSION_VEHICLE ||
        DistanceBetweenPoints(vehicle->GetPosition(), CVector(1543.2f, -1687.0f, 12.4f)) > 120.0f) return;
    bOgLocGuestCarTriggered = true;
    ogLocCarRef = ScriptParams[1];
    ogLocCarScript = script;
    ogLocCarSince = ogLocHeadingSince = GetTickCount();
    ogLocHeading = vehicle->GetHeading();
}


static void OnDriveThruCarHeadingSet(CRunningScript* script)
{
    if (!script || !script->m_bIsMission || _strnicmp(script->m_szName, "SWEET3", 8) != 0 ||
        bDriveThruGuestCarTriggered || !CConfigLoader::ms_bMissionDriveThruGuestCar ||
        !CConfigLoader::ms_bVanillaScripts || COpCodeSync::bProcessingNetworkOpcode ||
        !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script))
        return;
    CVehicle* vehicle = CPools::GetVehicle(ScriptParams[0]);
    if (!vehicle || vehicle->m_nCreatedBy != MISSION_VEHICLE ||
        !(DistanceBetweenPoints(vehicle->GetPosition(), CVector(2396.36f, -1917.96f, 12.38f)) <= 3.0f))
        return;
    if (!(fabsf(atan2f(sinf(vehicle->GetHeading() - 267.3f * 0.01745329252f),
        cosf(vehicle->GetHeading() - 267.3f * 0.01745329252f))) < 0.01f))
        return;
    bDriveThruGuestCarTriggered = true;
    // After the Cluckin' Bell cutscene the mission makes its Greenwood again: the guests' car (the one parked at the start
    // is released) appears behind it with the guests inside, one driving and the rest beside him (board off: parked only)
    if (CConfigLoader::ms_bMissionDriveThruFirstCar && CConfigLoader::ms_bMissionGuestVehiclesSelfBoard)
    {
        const size_t before = CCommandPutNetworkPlayersInCar::ParkedCopyCount();
        const int made = CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(vehicle, script, "drive-thru", 1, false,
            CConfigLoader::ms_bMissionGuestCarBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_BOARD
                                                    : CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY);
        if (made > 0)
            CCommandPutNetworkPlayersInCar::ReleaseParkedCopies(before);
        return;
    }
    if (CConfigLoader::ms_bMissionNoAutoBoard && CCommandPutNetworkPlayersInCar::MissionGuestCarMade())
    {
        CMissionMirror::LogEventf("rule-guest-vehicle case=drive-thru skipped=already-made");
        return;
    }
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(vehicle, script);
}

static CRunningScript* pHoodScript = nullptr;
static int nHoodMission = -1;
static bool bHoodEntered = false;
static bool bHoodDoorSendPending = false;
static bool bHoodDoorReopened = false;

static CPlayerPed* TrackHoodInterior()
{
    CRunningScript* hood = nullptr;
    CPlayerPed* player = FindPlayerPed(0);
    if (CMissionSyncRules::IsEnabled() && CConfigLoader::ms_bMissionHoodDoorOpen &&
        CConfigLoader::ms_bVanillaScripts && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CCoopMission::IsLocalIndividualMission() && !CCoopMission::HasLocalLeft() &&
        player && player->m_fHealth > 0.0f &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASDIED &&
        CWorld::Players[0].m_nPlayerState != PLAYERSTATE_HASBEENARRESTED)
    {
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && _strnicmp(script->m_szName, "SWEET1B", 8) == 0 &&
                COpCodeSync::IsScriptSynced(script))
            {
                hood = script;
                break;
            }
    }
    const int mission = CMissionMirror::GetRunningMissionId();
    if (!hood || hood != pHoodScript || mission != nHoodMission)
    {
        bHoodEntered = false;
        bHoodDoorSendPending = false;
        bHoodDoorReopened = false;
    }
    pHoodScript = hood;
    nHoodMission = mission;
    if (!hood)
        return nullptr;
    if (player->m_nAreaCode == 5)
        bHoodEntered = true;
    return player;
}

void CMissionSyncRules::SendHoodDoorCorrection()
{
    if (COpCodeSync::bProcessingNetworkOpcode)
        return;
    TrackHoodInterior();
    if (!bHoodDoorSendPending)
        return;
    bHoodDoorSendPending = false;
    CEntryExitMarkerSync::Send();
}

// What the story script gives the player (GIVE_WEAPON_TO_CHAR / SET_CHAR_AMMO / REMOVE_WEAPON_FROM_CHAR scplayer) reaches
// the guests too, same weapon and ammo: opcode sync only replays it on the host's remote ped. Nothing more: where vanilla
// gives CJ no weapon (Drive-By, Wrong Side) the guests get none either (the user, 15-11). mission-guest-weapon-parity
static void MirrorPlayerWeapon(CRunningScript* pScript, uint16_t opcode)
{
    if (opcode != 0x01B2 && opcode != 0x017B && opcode != 0x0555)
        return;
    if (!CConfigLoader::ms_bMissionGuestWeaponParity || !CConfigLoader::ms_bMissionPlayerEquipment ||
        !CMissionSyncRules::IsEnabled() || COpCodeSync::bProcessingNetworkOpcode || !pScript || !pScript->m_bIsMission ||
        !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript) ||
        !CMissionSyncRules::IsStoryScript(pScript))
        return;
    CPlayerPed* host = FindPlayerPed(0);
    if (!host || ScriptParams[0] != CPools::GetPedRef(host))
        return;
    const int weapon = ScriptParams[1];
    const int ammo = opcode == 0x0555 ? 0 : ScriptParams[2];
    if (weapon <= 0 || weapon > 46)
        return;
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        // every player of the mission, wherever he is (a late or far one must get it too)
        if (!CCoopMission::ShouldIncludeNetworkPlayer(guest))
            continue;
        Packets::Scripts::MissionPlayerEquipment equipment{};
        equipment.playerid = guest->m_playerId;
        equipment.weaponType = static_cast<uint8_t>(weapon);
        equipment.ammo = ammo;
        equipment.remove = opcode == 0x0555;
        equipment.setAmmo = opcode == 0x017B;
        GetPacketFactory().Send(equipment);
    }
    CMissionMirror::LogEventf("rule-guest-weapon script=%.8s op=%04X weapon=%d ammo=%d", pScript->m_szName, opcode, weapon, ammo);
}

void CMissionSyncRules::OnCommandExecuted(CRunningScript* pScript, uint16_t opcode)
{
    opcode &= 0x7FFF;
    MirrorPlayerWeapon(pScript, opcode);
    CJustBusinessEscort::OnCommandExecuted(pScript, opcode);
    if (!COpCodeSync::bProcessingNetworkOpcode && pScript && pScript->m_bIsMission &&
        _strnicmp(pScript->m_szName, "SWEET1B", 8) == 0)
    {
        const auto player = TrackHoodInterior();
        if (opcode == 0x07FB && ScriptParams[0] == 0 && player && bHoodEntered && player->m_nAreaCode == 0)
        {
            // Native 07FB sets bit 14 on every matching name, not just one linked endpoint.
            CEntryExitManager::SetEnabledByName("LACRAK", true);
            // The script repeats this close every frame while the host stays outside.
            if (!bHoodDoorReopened)
            {
                bHoodDoorReopened = true;
                bHoodDoorSendPending = true;
                CMissionMirror::LogEventf("rule-hood-door-open");
            }
        }
    }
    if (opcode == 0x0175 && IsEnabled())
    {
        OnDriveThruCarHeadingSet(pScript);
        OnOgLocBikeHeadingSet(pScript);
        OnJustBusinessBikeHeadingSet(pScript);
        OnMaddDoggBikeHeadingSet(pScript);
        return;
    }
    if (opcode == 0x0430 && IsEnabled())
    {
        OnOgLocPassengerWarp(pScript);
        OnManagementPassengerWarp(pScript);
    }
    if (opcode == 0x0360 && IsEnabled())
        OnMusic1GarageOpen(pScript);
    if (opcode == 0x06FD && IsEnabled())
        OnOgLocPlaybackSpeed(pScript);
    if (opcode == 0x05CD || opcode == 0x0633 || opcode == 0x0622 || opcode == 0x0362)
        OnScriptedExit(pScript, opcode);
    if (opcode == 0x01B2 || opcode == 0x01B9 || opcode == 0x0555 || opcode == 0x017B)
    {
        OnPlayerEquipment(pScript, opcode);
        return;
    }
    if (opcode == 0x0186 && IsEnabled())
    {
        OnCarBlipAdded(pScript);
        return;
    }
    if (opcode != 0x00A1 && opcode != 0x0362 && opcode != 0x036A && opcode != 0x0430)
        return;

    if (!MutesTeleportCommand(pScript) || COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript))
        return;

    // the char the command moved is still the first collected parameter
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!localPlayer || ScriptParams[0] != CPools::GetPedRef(localPlayer))
        return;

    // Home Invasion: each player walks into (and out of) the house by himself (T91).
    if (CHomeInvasion::SuppressGather(pScript))
        return;
    // Robbing Uncle Sam: every crate the host's forklift drops in the Mule warps CJ into the forklift (99@) for a short
    // scene; that is not a move of the story, the guests keep working with their own forklifts (2026-11-15: they were
    // gathered at the host after each delivery)
    if (opcode == 0x036A && CConfigLoader::ms_bCoopRobbingUncleSam && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(pScript->m_szName, "RYDER2", 8))
    {
        CVehicle* lift = CPools::GetVehicle(ScriptParams[1]);
        if (lift && lift->m_nModelIndex == 530)
            return;
    }
    // A passenger warp on Smoke's recorded bike must not eject guests from their own bikes.
    if (opcode == 0x0430 && CConfigLoader::ms_bCoopJustBusiness && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(pScript->m_szName, "DRUGS1", 8) && localPlayer->m_nPedFlags.bInVehicle &&
        localPlayer->m_pVehicle && localPlayer->m_pVehicle->m_nModelIndex == 581)
    {
        bTeleportPending = false;
        return;
    }

    // LA1FIN2 (coop-green-sabre): the host is warped into the Bravura (2 doors) to drive to Sweet while her health runs
    // down; the guests take its free seat and one shared 4-door car next to it instead of being left on foot.
    if (opcode == 0x036A && CConfigLoader::ms_bCoopGreenSabre && CConfigLoader::ms_bVanillaScripts &&
        !_strnicmp(pScript->m_szName, "LA1FIN2", 8))
    {
        static int lastBravura = -1;
        CVehicle* bravura = CPools::GetVehicle(ScriptParams[1]);
        if (bravura && bravura->m_nModelIndex == 401 && bravura->m_nCreatedBy == MISSION_VEHICLE &&
            lastBravura != ScriptParams[1])
        {
            lastBravura = ScriptParams[1];
            CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(bravura, pScript, "green-sabre", 1);
            // the ones within its reach (120 m) got a seat; the far ones are still gathered next to the host
            fGatherSkipNear = 120.0f;
            bTeleportPending = true;
            return;
        }
    }
    // Boarding owns the passenger distribution for this warp; never also gather on foot.
    if (opcode == 0x036A && CConfigLoader::ms_bMissionScriptedBoarding && pScript->m_bIsMission &&
        localPlayer->m_nPedFlags.bInVehicle &&
        CCommandPutNetworkPlayersInCar::HasMissionPassenger(localPlayer->m_pVehicle, true))
    {
        bTeleportPending = false;
        return;
    }
    // the players already next to the authority stay where they are (a marker's snap must not pull them in); T97: the
    // ground snap of the MUSIC1 arrival scene only gathers who is still far (the scene waited for the near ones)
    fGatherSkipNear = static_cast<float>(CConfigLoader::ms_nMissionGatherSkipNear);
    if (opcode == 0x00A1 && nBeachArrivalTick && GetTickCount() - nBeachArrivalTick < 2000 &&
        CConfigLoader::ms_bMusic1BeachArrival && _strnicmp(pScript->m_szName, "MUSIC1", 8) == 0)
    {
        fGatherSkipNear = 60.0f;
        nBeachArrivalTick = 0;
    }
    bTeleportPending = true;
}

// a synced story mission runs here (this game is its authority)
static bool IsStoryMissionRunning()
{
    for (CRunningScript* script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
    {
        if (script->m_bIsMission && CMissionSyncRules::IsStoryScript(script) && COpCodeSync::IsScriptSynced(script))
            return true;
    }
    return false;
}

// Original scripts only (ours give each player his own vehicle by hand in the missions that need it). A two-wheeler the
// mission marks for the player (ADD_BLIP_FOR_CAR on a mission bike: Big Smoke's BMX) has no seat for the others, so each
// player of the mission gets an equal one parked next to it, with his own blip, and gets on it by himself; the blip of
// the authority's bike is not mirrored (each one has his own). Entering an empty mission car does nothing;
// NPC boarding is handled separately.
static constexpr int MAX_OBJECTIVE_VEHICLES = 8;
static int aObjectiveVehicles[MAX_OBJECTIVE_VEHICLES];
static int nObjectiveVehicles = 0;

static void OnOgLocBikeHeadingSet(CRunningScript* pScript)
{
    if (bOgLocBikesTriggered || !pScript || !pScript->m_bIsMission || _strnicmp(pScript->m_szName, "TWAR7", 8) != 0 ||
        !CConfigLoader::ms_bMissionOgLocBikes || !CConfigLoader::ms_bVanillaScripts ||
        COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(pScript))
        return;

    // SET_CAR_HEADING has applied the final mission orientation.
    const int vehicleRef = ScriptParams[0];
    CVehicle* vehicle = CPools::GetVehicle(vehicleRef);
    if (!vehicle || vehicle->m_nModelIndex != 461 || vehicle->m_nCreatedBy != MISSION_VEHICLE ||
        !(DistanceBetweenPoints(vehicle->GetPosition(), CVector(2471.0f, -1285.1f, 28.3f)) <= 3.0f))
        return;
    if (fabsf(atan2f(sinf(vehicle->GetHeading() - 272.5f * 0.01745329252f),
        cosf(vehicle->GetHeading() - 272.5f * 0.01745329252f))) >= 0.01f) return;
    for (int i = 0; i < nObjectiveVehicles; i++)
        if (aObjectiveVehicles[i] == vehicleRef)
            return;
    if (nObjectiveVehicles >= MAX_OBJECTIVE_VEHICLES)
        return;
    aObjectiveVehicles[nObjectiveVehicles++] = vehicleRef;
    bOgLocBikesTriggered = true;

    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(vehicle, pScript, "og-loc-bikes", 2, true);
}

static bool IsTwoWheeler(CVehicle* pVehicle)
{
    return pVehicle->m_nVehicleSubType == static_cast<unsigned int>(VEHICLE_BIKE) ||
           pVehicle->m_nVehicleSubType == static_cast<unsigned int>(VEHICLE_BMX);
}

// ADD_BLIP_FOR_CAR (0186) has run: ScriptParams[0] holds the new blip
static void OnCarBlipAdded(CRunningScript* pScript)
{
    // Big Smoke: every guest needs a bike to follow him, with or without the other car copies
    if (!pScript || (!CConfigLoader::ms_bMissionGuestCarCopies && _strnicmp(pScript->m_szName, "INTRO1", 8) != 0) ||
        !CConfigLoader::ms_bVanillaScripts ||
        COpCodeSync::bProcessingNetworkOpcode || !CNetwork::m_bConnected ||
        !CCoopMission::IsScriptAuthority() || !CMissionSyncRules::IsStoryScript(pScript) ||
        !COpCodeSync::IsScriptSynced(pScript))
        return;

    const int blip = ScriptParams[0];
    const int index = CRadar::GetActualBlipArrayIndex(blip);
    if (index < 0)
        return;
    const int vehicleRef = static_cast<int>(CRadar::ms_RadarTrace[index].m_nEntityHandle);
    CVehicle* pVehicle = CPools::GetVehicle(vehicleRef);
    if (!pVehicle || pVehicle->m_nCreatedBy != MISSION_VEHICLE || !IsTwoWheeler(pVehicle))
        return;

    CMirrorBlips::Disown(static_cast<uint32_t>(blip));

    for (int i = 0; i < nObjectiveVehicles; i++)
    {
        if (aObjectiveVehicles[i] == vehicleRef)
            return;  // the blip again (the authority got off it): the copies are already there
    }
    if (nObjectiveVehicles < MAX_OBJECTIVE_VEHICLES)
        aObjectiveVehicles[nObjectiveVehicles++] = vehicleRef;

    const int copies = CCommandPutNetworkPlayersInCar::ParkCopiesNextTo(pVehicle);
    CMissionMirror::LogEventf("rule-objective-vehicle model=%d copies=%d", pVehicle->m_nModelIndex, copies);
}

static bool IsMissionNpcEnteringVehicle(CVehicle* vehicle)
{
    if (!CPools::ms_pPedPool)
        return false;
    for (int i = 0; i < CPools::ms_pPedPool->m_nSize; i++)
    {
        CPed* ped = CPools::ms_pPedPool->GetAt(i);
        if (!ped || ped->IsPlayer() || CNetworkPlayerManager::GetPlayer(ped) || ped->m_nCreatedBy != 2 ||
            ped->m_nPedFlags.bInVehicle || !ped->m_pIntelligence)
            continue;
        for (auto primary : ped->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks)
            for (CTask* task = primary; task; task = task->GetSubTask())
                if ((task->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER ||
                     task->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_DRIVER) &&
                    static_cast<CTaskComplexEnterCar*>(task)->m_pTargetVehicle == vehicle)
                    return true;
    }
    return false;
}

// T98: the mission's own vehicle, from the pool: the nearest mission-made one of this model around its spawn
static CVehicle* FindMissionVehicle(int model, const CVector& at, float radius)
{
    CVehicle* best = nullptr;
    float bestDistance = radius;
    if (!CPools::ms_pVehiclePool)
        return nullptr;
    for (int i = 0; i < CPools::ms_pVehiclePool->m_nSize; ++i)
    {
        CVehicle* vehicle = CPools::ms_pVehiclePool->GetAt(i);
        if (!vehicle || vehicle->m_nModelIndex != model || vehicle->m_nCreatedBy != MISSION_VEHICLE ||
            vehicle->m_fHealth <= 0.0f || vehicle->m_nAreaCode != 0 || !IsMissionCleanupVehicle(vehicle))
            continue;
        const float distance = DistanceBetweenPoints(vehicle->GetPosition(), at);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = vehicle;
        }
    }
    return best;
}

// T98 (mission-guest-vehicles-self-board): once the mission has made the host's vehicle, the guests' ones are parked
// next to it with their blip (the fewest the rule allows) and each guest gets on by himself; nobody is put in them
static bool ParkGuestVehiclesWhenCreated(CRunningScript* active, bool& parked, int model, const CVector& at, const char* rule,
    int perVehicle = 1, float radius = 80.0f)
{
    if (!CConfigLoader::ms_bMissionGuestVehiclesSelfBoard || !active || parked || CCutsceneMgr::ms_running ||
        TheCamera.m_bWideScreenOn)
        return false;
    CVehicle* car = FindMissionVehicle(model, at, radius);
    CPlayerPed* host = FindPlayerPed(0);
    if (!car || !host || host->m_fHealth <= 0.0f)
        return false;
    parked = true;
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, rule, perVehicle, false,
        CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY);
    return true;
}

// coop-countryside: Catalina rides in the host's free seat, so the guests need their own.
// CAT1 Local Liquor Store: the host takes one of the gang's quads (471, 82@) for the chase: a quad for each two guests.
// CAT2 Small Town Bank: the escape bikes (523) behind the bank, 88@ the host's (CAT2.txt 2359): a bike for each two.
// CAT3 Tanker Commander: the tanker cab (514, 2 seats, CAT3.txt 137): one shared 4-door car next to it.
// TRUTH1 Body Harvest: the combine (532, driver only) in the field; TRUTH2: The Truth's camper (483) by the weed.
static void ProcessCountrysideGuestVehicles()
{
    static CRunningScript* attempt = nullptr;
    static bool parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopCountryside && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && COpCodeSync::IsScriptSynced(script) &&
                (!_strnicmp(script->m_szName, "CAT1", 8) || !_strnicmp(script->m_szName, "CAT2", 8) ||
                 !_strnicmp(script->m_szName, "CAT3", 8) || !_strnicmp(script->m_szName, "TRUTH1", 8) ||
                 !_strnicmp(script->m_szName, "TRUTH2", 8))) active = script;
    if (!active || active != attempt) parked = false;
    attempt = active;
    if (!active || parked)
        return;
    if (!_strnicmp(active->m_szName, "CAT2", 8))
        ParkGuestVehiclesWhenCreated(active, parked, 523, CVector(2323.166f, 70.137f, 26.04f), "countryside", 2, 5.0f);
    else if (!_strnicmp(active->m_szName, "CAT3", 8))
        ParkGuestVehiclesWhenCreated(active, parked, 514, CVector(657.49f, -585.13f, 16.36f), "countryside", 1, 20.0f);
    else if (!_strnicmp(active->m_szName, "TRUTH1", 8))
        ParkGuestVehiclesWhenCreated(active, parked, 532, CVector(-1055.0f, -1035.0f, 128.3f), "countryside", 1, 150.0f);
    else if (!_strnicmp(active->m_szName, "TRUTH2", 8))
        ParkGuestVehiclesWhenCreated(active, parked, 483, CVector(-1077.6f, -1650.2f, 75.3f), "countryside", 1, 60.0f);
    else
    {
        // the gang rides the same quads before the shootout: wait until the host drives one
        auto* host = FindPlayerPed(0);
        auto* quad = host && host->m_nPedFlags.bInVehicle ? host->m_pVehicle : nullptr;
        if (!quad || quad->m_nModelIndex != 471 || quad->m_nCreatedBy != MISSION_VEHICLE || quad->m_pDriver != host ||
            CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn)
            return;
        parked = true;
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(quad, active, "countryside", 2, false,
            CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY
                                                              : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
    }
}

// the ammo a guest gets with a weapon the rules hand out (Drive-By, Wrong Side of the Tracks): what the script gave the
// player. These scripts give CJ no Tec9 (only their allies, 30000 / 99999 each), so a guest gets what the host holds of
// that weapon, or two clips when he holds none, not a made-up 1000 (the user, 2026-11-14). mission-equipment-script-ammo
static int GuestRuleAmmo(eWeaponType type, int legacyAmmo)
{
    if (!CConfigLoader::ms_bMissionEquipmentScriptAmmo)
        return legacyAmmo;
    const auto info = CWeaponInfo::GetWeaponInfo(type, 1);
    if (!info)
        return legacyAmmo;
    CPlayerPed* host = FindPlayerPed(0);
    if (host && info->m_nSlot >= 0 && info->m_nSlot < 13 && host->m_aWeapons[info->m_nSlot].m_eWeaponType == type &&
        host->m_aWeapons[info->m_nSlot].m_nTotalAmmo > 0)
        return (std::min)(static_cast<int>(host->m_aWeapons[info->m_nSlot].m_nTotalAmmo), legacyAmmo);
    return (std::max)(1, 2 * static_cast<int>(info->m_nAmmoClip));
}

// SWEET4 needs all three passenger seats for its scripted allies.
static void ProcessDriveByGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool distributed = false, parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopDriveBy && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "SWEET4", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) distributed = parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 492, CVector(2517.0f, -1676.0f, 13.0f), "drive-by");
    auto* host = FindPlayerPed(0);
    if (!active || distributed || !host || host->m_fHealth <= 0.0f ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn ||
        !host->m_nPedFlags.bInVehicle || !host->m_pVehicle) return;
    auto* car = host->m_pVehicle;
    if (car->m_nModelIndex != 492 || car->m_nCreatedBy != MISSION_VEHICLE ||
        car->m_pDriver != host || car->m_fHealth <= 0.0f ||
        DistanceBetweenPoints(car->GetPosition(), CVector(2517.0f, -1676.0f, 13.0f)) > 80.0f) return;
    distributed = true;
    bTeleportPending = false;
    // Existing vehicles remain usable; every nearby participant can shoot from them too.
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        if (!CCoopMission::ShouldIncludeNetworkPlayer(guest) || !guest->m_pPed ||
            guest->m_bDead || guest->m_pPed->m_fHealth <= 0.0f ||
            !CConfigLoader::ms_bMissionPlayerEquipment || CConfigLoader::ms_bMissionGuestWeaponParity) continue;
        // every player of the mission, wherever he is (a late one would never get it otherwise)
        Packets::Scripts::MissionPlayerEquipment equipment{};
        equipment.playerid = guest->m_playerId;
        equipment.weaponType = WEAPON_TEC9;
        equipment.ammo = GuestRuleAmmo(WEAPON_TEC9, 1000);
        GetPacketFactory().Send(equipment);
    }
    // parked when the mission made the Greenwood (no free seat in the host's: the three allies)
    if (!CConfigLoader::ms_bMissionGuestVehiclesSelfBoard)
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "drive-by", 1);
}

// Keep the native allies in their seats; guests drive their own nearby copies.
static void ProcessLosSepulcrosGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool outbound = false, returning = false, parkedOut = false, parkedBack = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopLosSepulcros && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "SWEET7", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) outbound = returning = parkedOut = parkedBack = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parkedOut, 492, CVector(2508.0f, -1671.0f, 13.0f), "los-sepulcros");
    // the Admiral is made at the start, with the guests still in Grove: the way back is parked when the host drives off
    auto* host = FindPlayerPed(0);
    if (!active || !host || host->m_fHealth <= 0.0f || CCutsceneMgr::ms_running ||
        TheCamera.m_bWideScreenOn || !host->m_nPedFlags.bInVehicle || !host->m_pVehicle) return;
    auto* car = host->m_pVehicle;
    if (car->m_nCreatedBy != MISSION_VEHICLE || car->m_pDriver != host || car->m_fHealth <= 0.0f) return;
    bool* distributed = nullptr;
    if (car->m_nModelIndex == 492 &&
        DistanceBetweenPoints(car->GetPosition(), CVector(2508.0f, -1671.0f, 13.0f)) < 80.0f)
        distributed = &outbound;
    else if (car->m_nModelIndex == 445 &&
        DistanceBetweenPoints(car->GetPosition(), CVector(925.7f, -1123.0f, 23.0f)) < 80.0f)
        distributed = &returning;
    if (!distributed || *distributed) return;
    *distributed = true;
    bTeleportPending = false;
    // no host seats here (the allies ride with him): parked when the mission made the car
    if (!CConfigLoader::ms_bMissionGuestVehiclesSelfBoard)
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "los-sepulcros", 1);
    // the way back: the guests on foot whose Greenwood is gone get one car by the Admiral
    else if (distributed == &returning && !parkedBack &&
        CCommandPutNetworkPlayersInCar::CountParkedCopiesNear(car->GetPosition(), 150.0f) == 0)
    {
        parkedBack = true;
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "los-sepulcros", 1, false,
            CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY);
    }
}

// RYDER2 "Robbing Uncle Sam": the Mule has Ryder's seat only, the guests get one Greenwood parked by it (asked 2026-11-13)
static void ProcessRobbingUncleSamGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopRobbingUncleSam && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "RYDER2", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 414, CVector(2475.9f, -1679.6f, 12.3f), "robbing-uncle-sam");
}

// GUNS1 "Home Invasion" (coop-home-invasion): the BOXBURG (609) seats the host, Ryder and two more; when more guests come
// than those free seats (4 players: 3 guests) ONE Greenwood is parked by it for all of them (asked 2026-11-16)
static void ProcessHomeInvasionGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopHomeInvasion && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "GUNS1", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 609, CVector(2476.68f, -1679.57f, 12.35f), "home-invasion");
}

// SWEET3 "Drive-Thru" (mission-drive-thru-first-car): the Greenwood of the first leg (Grove to the Cluckin' Bell) is made
// before the opening cutscene; the guests' car is parked next to it as soon as the cutscene is over, like Drive-By
// (the second car, after the Cluckin' Bell cutscene, is made by OnDriveThruCarHeadingSet)
static void ProcessDriveThruGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bMissionDriveThruFirstCar && CConfigLoader::ms_bMissionDriveThruGuestCar &&
        CConfigLoader::ms_bVanillaScripts && CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "SWEET3", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 492, CVector(2508.7f, -1671.7f, 12.3f), "drive-thru");
}

// RYDER3 "Catalyst" (mission-catalyst-guest-car): the host drives the mission's Picador (600) beside the train; ONE extra car of
// the same model is parked next to it for the guests to drive (the second guest rides along). Crates dropped into it are not
// counted: the script only counts what Ryder catches. With the switch off the guests ride the train (mission-guest-train).
static void ProcessCatalystGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool parked = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bMissionCatalystGuestCar && CConfigLoader::ms_bVanillaScripts && CMissionSyncRules::IsEnabled() &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "RYDER3", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    if (!active || active != attempt) parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 600, CVector(2476.035f, -1678.379f, 12.3457f), "catalyst", 1, 100.0f);
}

static void ProcessRunningDogGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool distributed = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopRunningDog && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "SMOKE2", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    static bool parked = false;
    if (!active || active != attempt) distributed = parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 466, CVector(2068.42f, -1694.87f, 13.3f), "running-dog");
    auto* host = FindPlayerPed(0);
    if (!active || distributed || !host || host->m_fHealth <= 0.0f ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn ||
        !host->m_nPedFlags.bInVehicle || !host->m_pVehicle) return;
    auto* car = host->m_pVehicle;
    if (car->m_nModelIndex != 466 || car->m_nCreatedBy != MISSION_VEHICLE ||
        car->m_pDriver != host || car->m_fHealth <= 0.0f ||
        DistanceBetweenPoints(car->GetPosition(), CVector(2068.42f, -1694.87f, 13.3f)) > 80.0f) return;
    distributed = true;
    bTeleportPending = false;
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "running-dog", 1, false,
        CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_HOST_SEATS_ONLY
                                                         : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
}

static void ProcessWrongSideTracksGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool outbound = false, pursuit = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopWrongSideTracks && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "SMOKE3", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    static bool parkedOut = false, parkedBikes = false;
    if (!active || active != attempt) outbound = pursuit = parkedOut = parkedBikes = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parkedOut, 466, CVector(2058.86f, -1694.61f, 13.3f), "wrong-side-tracks");
    ParkGuestVehiclesWhenCreated(active, parkedBikes, 468, CVector(1778.62f, -1939.91f, 13.6f), "wrong-side-tracks");
    auto* host = FindPlayerPed(0);
    if (!active || !host || host->m_fHealth <= 0.0f ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn ||
        !host->m_nPedFlags.bInVehicle || !host->m_pVehicle) return;
    auto* car = host->m_pVehicle;
    if (car->m_nCreatedBy != MISSION_VEHICLE || car->m_pDriver != host || car->m_fHealth <= 0.0f) return;
    bool* distributed = nullptr;
    if (car->m_nModelIndex == 466 &&
        DistanceBetweenPoints(car->GetPosition(), CVector(2058.86f, -1694.61f, 13.3f)) < 80.0f)
        distributed = &outbound;
    else if (car->m_nModelIndex == 468 &&
        DistanceBetweenPoints(car->GetPosition(), CVector(1778.62f, -1939.91f, 13.6f)) < 80.0f)
        distributed = &pursuit;
    if (!distributed || *distributed) return;
    *distributed = true;
    bTeleportPending = false;
    if (car->m_nModelIndex == 468 && CConfigLoader::ms_bMissionPlayerEquipment && !CConfigLoader::ms_bMissionGuestWeaponParity)
        for (auto guest : CNetworkPlayerManager::m_pPlayers)
        {
            if (!CCoopMission::ShouldIncludeNetworkPlayer(guest) || !guest->m_pPed || guest->m_bDead ||
                guest->m_pPed->m_fHealth <= 0.0f || guest->m_pPed->m_nAreaCode != car->m_nAreaCode ||
                DistanceBetweenPoints(guest->m_pPed->GetPosition(), car->GetPosition()) > 120.0f) continue;
            Packets::Scripts::MissionPlayerEquipment equipment{};
            equipment.playerid = guest->m_playerId;
            equipment.weaponType = WEAPON_TEC9;
            equipment.ammo = GuestRuleAmmo(WEAPON_TEC9, 1000);
            GetPacketFactory().Send(equipment);
        }
    // the bikes were parked when the mission made Smoke's Sanchez; the 466 keeps its free seats for the guests
    if (!CConfigLoader::ms_bMissionGuestVehiclesSelfBoard)
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "wrong-side-tracks", 1);
    else if (car->m_nModelIndex == 466)
        CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "wrong-side-tracks", 1, false,
            CCommandPutNetworkPlayersInCar::GUEST_HOST_SEATS_ONLY);
}

static void ProcessJustBusinessGuestCars()
{
    static CRunningScript* attempt = nullptr;
    static bool distributed = false;
    CRunningScript* active = nullptr;
    if (CConfigLoader::ms_bCoopJustBusiness && CConfigLoader::ms_bVanillaScripts &&
        CMissionSyncRules::IsEnabled() && CNetwork::m_bConnected &&
        !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority())
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && !_strnicmp(script->m_szName, "DRUGS1", 8) &&
                COpCodeSync::IsScriptSynced(script)) active = script;
    static bool parked = false;
    if (!active || active != attempt) distributed = parked = false;
    attempt = active;
    ParkGuestVehiclesWhenCreated(active, parked, 466, CVector(2058.86f, -1694.612f, 12.297f), "just-business");
    auto* host = FindPlayerPed(0);
    if (!active || !host || host->m_fHealth <= 0.0f ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn ||
        !host->m_nPedFlags.bInVehicle || !host->m_pVehicle) return;
    auto* car = host->m_pVehicle;
    if (car->m_nCreatedBy != MISSION_VEHICLE || car->m_fHealth <= 0.0f) return;
    if (distributed || car->m_nModelIndex != 466 || car->m_pDriver != host ||
        DistanceBetweenPoints(car->GetPosition(), CVector(2058.86f, -1694.612f, 12.297f)) > 80.0f) return;
    distributed = true;
    bTeleportPending = false;
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, active, "just-business", 1, false,
        CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_HOST_SEATS_ONLY
                                                         : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
}

// Trip skip (CGameLogic::SetUpSkip / UpdateSkip, gta-reversed GameLogic.cpp 0x4423C0 / 0x442480): the scripts offer it with the
// SKIP opcodes and only the authority's game runs it (DPad right: fade, then the player and his vehicle are moved to
// SkipPosition). Nothing moves the guests, so each one far from the host is moved to him and, once the skip is over, put in
// a free seat of his vehicle or in the one shared car beside it (mission-trip-skip-guests). Any mission that offers the skip.
static void ProcessTripSkip()
{
    constexpr uintptr_t SKIP_STATE_ADDR = 0x96A8A0, SKIP_POSITION_ADDR = 0x96A8D4;
    constexpr int SKIP_IN_PROGRESS = 2, SKIP_FADING_OUT = 3, SKIP_WAITING_SCRIPT = 5;
    static int previousState = 0;
    static bool pending = false;
    static DWORD movedAt = 0;
    static std::vector<playerid_t> moved;
    const int state = *reinterpret_cast<const int*>(SKIP_STATE_ADDR);
    CPlayerPed* host = FindPlayerPed(0);
    CRunningScript* mission = nullptr;
    if (CConfigLoader::ms_bMissionTripSkipGuests && CConfigLoader::ms_bVanillaScripts && CMissionSyncRules::IsEnabled() &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() && host &&
        host->m_fHealth > 0.0f)
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsMission && CMissionSyncRules::IsStoryScript(script) && COpCodeSync::IsScriptSynced(script))
                mission = script;
    const int wasState = previousState;
    previousState = state;
    if (!mission)
    {
        pending = false;
        moved.clear();
        return;
    }
    const DWORD now = GetTickCount();
    // the frame the game moved the host: the state left IN_PROGRESS and he stands at the skip position
    if (!pending && wasState == SKIP_IN_PROGRESS && state != SKIP_IN_PROGRESS)
    {
        const CVector skip = *reinterpret_cast<const CVector*>(SKIP_POSITION_ADDR);
        const CVector at = FindPlayerCoors(0);
        if ((at.x - skip.x) * (at.x - skip.x) + (at.y - skip.y) * (at.y - skip.y) < 40.0f * 40.0f)
        {
            moved.clear();
            CNetworkPlayer* players[3];
            int count = 0;
            CVehicle* car = host->m_nPedFlags.bInVehicle ? host->m_pVehicle : nullptr;
            for (auto guest : CNetworkPlayerManager::m_pPlayers)
            {
                CPed* ped = guest->m_pPed;
                if (count >= 3 || !ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
                    (car && ped->m_nPedFlags.bInVehicle && ped->m_pVehicle == car))
                    continue;
                const CVector pos = ped->m_nPedFlags.bInVehicle && ped->m_pVehicle ? ped->m_pVehicle->GetPosition() : ped->GetPosition();
                if (ped->m_nAreaCode == host->m_nAreaCode && (pos.x - at.x) * (pos.x - at.x) + (pos.y - at.y) * (pos.y - at.y) <= 30.0f * 30.0f)
                    continue;
                // out of his car first (the same order as a scripted warp of the host), then next to the host
                if (ped->m_nPedFlags.bInVehicle)
                {
                    Packets::Scripts::MissionScriptedExit exitPacket{};
                    exitPacket.playerid = guest->m_playerId;
                    exitPacket.opcode = 0x0362;
                    GetPacketFactory().Send(exitPacket);
                }
                players[count++] = guest;
                moved.push_back(guest->m_playerId);
            }
            CCommandTeleportPlayersToHostSafely::TeleportToHost(players, count);
            pending = count > 0;
            movedAt = now;
            CMissionMirror::LogEventf("rule-trip-skip players=%d state=%d host-in-car=%d", count, state, car ? 1 : 0);
        }
    }
    if (!pending)
        return;
    // the skip is over (a script-finished one clears it later) and the scene around the host has settled
    const bool over = state != SKIP_IN_PROGRESS && state != SKIP_FADING_OUT && state != SKIP_WAITING_SCRIPT;
    if (!(over && now - movedAt >= 3000) && now - movedAt < 20000)
        return;
    pending = false;
    CVehicle* car = host->m_nPedFlags.bInVehicle ? host->m_pVehicle : nullptr;
    if (car && CNetworkVehicleManager::GetVehicle(car))
    {
        // the guests' car of the first part stays behind: the new one appears here
        CCommandPutNetworkPlayersInCar::ReleaseParkedCopiesFar(car->GetPosition(), 80.0f, moved);
        CCommandPutNetworkPlayersInCar::BoardGuestsAfterTripSkip(car, mission, moved);
    }
    moved.clear();
}

static void ProcessMissionBoarding()
{
    static int vehicleRef = -1;
    static DWORD enteredAt = 0;
    static bool distributed = false;
    CPlayerPed* player = FindPlayerPed(0);
    const bool eligible = CConfigLoader::ms_bMissionScriptedBoarding && CMissionSyncRules::IsEnabled() &&
        CNetwork::m_bConnected && !COpCodeSync::bProcessingNetworkOpcode && CCoopMission::IsScriptAuthority() &&
        !CMirrorBlips::IsPerPlayerMissionRunning() && IsStoryMissionRunning() && player && player->m_fHealth > 0.0f;
    if (!eligible || !player->m_nPedFlags.bInVehicle || !player->m_pVehicle)
    {
        vehicleRef = -1;
        distributed = false;
        return;
    }
    if ((CConfigLoader::ms_bCoopDriveBy && CMissionMirror::GetRunningMissionId() == 17) ||
        (CConfigLoader::ms_bCoopLosSepulcros && CMissionMirror::GetRunningMissionId() == 20) ||
        (CConfigLoader::ms_bCoopRunningDog && CMissionMirror::GetRunningMissionId() == 28) ||
        (CConfigLoader::ms_bCoopWrongSideTracks && CMissionMirror::GetRunningMissionId() == 29) ||
        (CConfigLoader::ms_bCoopJustBusiness && CMissionMirror::GetRunningMissionId() == 30) ||
        (CConfigLoader::ms_bCoopManagementIssues && CMissionMirror::GetRunningMissionId() == 33) ||
        (CConfigLoader::ms_bMissionDriveThruFirstCar && CConfigLoader::ms_bMissionDriveThruGuestCar &&
         CConfigLoader::ms_bMissionGuestVehiclesSelfBoard && CMissionMirror::GetRunningMissionId() == 15) ||
        (CConfigLoader::ms_bMissionCatalystGuestCar && CMissionMirror::GetRunningMissionId() == 25)) return;
    CVehicle* vehicle = player->m_pVehicle;
    const int currentRef = CPools::GetVehicleRef(vehicle);
    if (vehicleRef != currentRef)
    {
        vehicleRef = currentRef;
        enteredAt = GetTickCount();
        distributed = false;
    }
    const bool missionNpc = CCommandPutNetworkPlayersInCar::HasMissionPassenger(vehicle);
    if (!distributed && CCommandPutNetworkPlayersInCar::HasMissionPassenger(vehicle, true))
        bTeleportPending = false;
    if (distributed || !missionNpc ||
        (GetTickCount() - enteredAt < 3000 && IsMissionNpcEnteringVehicle(vehicle)) || vehicle->m_fHealth <= 0.0f ||
        !CNetworkVehicleManager::GetVehicle(vehicle))
        return;
    distributed = true;
    bTeleportPending = false;
    CCommandPutNetworkPlayersInCar::PutPlayersInCar(vehicle, true);
}

// These launchers test 03EE before writing $onmission and create no contact blips.
namespace
{
struct SGuestLauncher
{
    const char* name;
    int chain;
    int counterOffset;
    int endCounter;
    int prerequisiteChain;
    int prerequisiteCounter;
    bool searched = false;
    DWORD lastStart = 0;
    int offset = -1;
    // what this game took from the host and never saves (each player keeps his own progress, the user 12.11)
    int ownCounter = -1;     // the counter before it was raised to the host's
    int copiedCounter = -1;  // the host's value it was raised to
    CRunningScript* started = nullptr;
};
SGuestLauncher s_aGuestLaunchers[] = {
    {"SWEET", 0, 1808, 9, 0, 0},
    {"RYDER", 1, 1812, 3, 0, 5},
    {"SMOKE", 2, 1816, 4, 0, 4},
    {"STRAP", 3, 1820, 5, 2, 1},
    // MOB_LA1 starts CRASH after OG Loc's second mission and CESAR after Sweet's seventh; FLOW starts LA1FIN with every
    // Los Santos chain done; LA1FIN2 (The Green Sabre) starts BCRASH. Their contact points come from INITIAL.
    {"CRASH", 4, 1824, 2, 3, 2},
    {"CESAR", 5, 1828, 1, 0, 7},
    {"LA1FIN", 6, 1832, 2, 0, 9},
    {"BCRASH", 9, 1972, 1, 6, 2},
    // MOB_CAT (started by LA1FIN2) phones CAT in once Badlands is passed, and TRU with Catalina's first date done.
    // BCESAR (Wu Zi Mu races) stays out: it also starts the race tournament (35), one copy per player.
    {"CAT", 7, 256, 4, 9, 1},
    {"TRU", 8, 1964, 2, 7, 1},
};

bool IsActiveScript(CRunningScript* script)
{
    for (auto active = CTheScripts::pActiveScripts; active; active = active->m_pNext)
        if (active == script)
            return true;
    return false;
}

// CGenericGameStorage::GenericSave (0x5D13E0), called by C_PcSave::SaveSlot at 0x619081: the save holds this player's own
// progress only. The launchers started from the host's and the counters raised to his are taken out while it writes.
bool __cdecl GenericSave_OwnProgress(int arg)
{
    CRunningScript* removed[std::size(s_aGuestLaunchers)]{};
    int raised[std::size(s_aGuestLaunchers)];
    for (size_t i = 0; i < std::size(s_aGuestLaunchers); ++i)
    {
        auto& launcher = s_aGuestLaunchers[i];
        int& counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[launcher.counterOffset]);
        raised[i] = -1;
        if (launcher.ownCounter >= 0 && counter == launcher.copiedCounter)
        {
            raised[i] = counter;
            counter = launcher.ownCounter;
        }
        // own progress past the chain's first mission: the launcher is his too. Taken out, a loaded save had no Sweet launcher
        // at all and Cesar Vialpando never started for that host (testeo 14-11)
        if (launcher.started && counter <= 0 && IsActiveScript(launcher.started) &&
            _strnicmp(launcher.started->m_szName, launcher.name, 8) == 0)
        {
            launcher.started->RemoveScriptFromList(&CTheScripts::pActiveScripts);
            removed[i] = launcher.started;
        }
        if (raised[i] >= 0 || removed[i])
            CMissionMirror::LogEventf("guest-launcher save-own name=%s counter=%d thread=%d", launcher.name, counter, removed[i] ? 0 : 1);
    }
    int& intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
    const int introRaised = CNetworkStaticBlip::ms_nIntroOwnCounter >= 0 && intro == CNetworkStaticBlip::ms_nIntroCopiedCounter
        ? intro : -1;
    if (introRaised >= 0)
        intro = CNetworkStaticBlip::ms_nIntroOwnCounter;
    CNetworkStaticBlip::BeforeSave();
    const bool result = plugin::CallAndReturn<bool, 0x5D13E0, int>(arg);
    CNetworkStaticBlip::AfterSave();
    if (introRaised >= 0)
        intro = introRaised;
    for (size_t i = 0; i < std::size(s_aGuestLaunchers); ++i)
    {
        if (raised[i] >= 0)
            *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[s_aGuestLaunchers[i].counterOffset]) = raised[i];
        if (removed[i])
            removed[i]->AddScriptToList(&CTheScripts::pActiveScripts);
    }
    return result;
}
}

int CMissionSyncRules::OwnChainCounter(int chain, int current)
{
    for (const auto& launcher : s_aGuestLaunchers)
        if (launcher.chain == chain && launcher.ownCounter >= 0 && current == launcher.copiedCounter)
            return launcher.ownCounter;
    return current;
}

void CMissionSyncRules::ForgetOwnCounters()
{
    for (auto& launcher : s_aGuestLaunchers)
        launcher.ownCounter = launcher.copiedCounter = -1;
    CNetworkStaticBlip::ms_nIntroOwnCounter = CNetworkStaticBlip::ms_nIntroCopiedCounter = -1;
}

static void ProcessGuestStoryLaunchers()
{
    auto& launchers = s_aGuestLaunchers;
    static unsigned int lastFrame = 0;
    const unsigned int frame = CTimer::m_FrameCounter;
    if (frame < lastFrame)
    {
        for (auto& launcher : launchers)
        {
            launcher.searched = false;
            launcher.offset = -1;
            launcher.lastStart = 0;
            launcher.ownCounter = launcher.copiedCounter = -1;
            launcher.started = nullptr;
        }
        CNetworkStaticBlip::ms_nIntroOwnCounter = CNetworkStaticBlip::ms_nIntroCopiedCounter = -1;
    }
    lastFrame = frame;
    CPlayerPed* player = FindPlayerPed(0);
    if (!CMissionSyncRules::IsEnabled() || !CConfigLoader::ms_bGuestStoryLaunchers ||
        !CConfigLoader::ms_bVanillaScripts || !CNetwork::m_bConnected || !CNetwork::m_bAuthenticated ||
        COpCodeSync::bProcessingNetworkOpcode || !player ||
        player->m_fHealth <= 0.0f || CWorld::Players[0].m_nPlayerState != PLAYERSTATE_PLAYING ||
        CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn || !CTheScripts::pActiveScripts ||
        *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1636]) != 0)
        return;

    // the host follows no one: only his own launchers missing from a save are started again (below)
    const bool authority = CCoopMission::IsScriptAuthority();

    // intro-follow-host: a save of this game went on with Sweet but its intro counter stayed behind (the guest played
    // Big Smoke/Ryder on the host's game, testeo 13-11): the old CJ/Ryder blip showed again after loading it
    if (!authority && CConfigLoader::ms_bIntroFollowHost &&
        CMissionSyncRules::OwnChainCounter(0, *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1808])) > 0 &&
        CNetworkStaticBlip::ms_nIntroOwnCounter < 0)
    {
        const int intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
        if (intro == 0 || intro == 1)
            CNetworkStaticBlip::SetIntroCounter(2);
    }

    // INT (Big Smoke / Ryder) gone while the intro is still pending: it terminated at a counter of 2 that came back down
    // (testeo 14-11: no "waiting" at Ryder's marker for the guest). Started again like the other guest launchers.
    if (!authority)
    {
        static int intOffset = -2;
        static DWORD intLastStart = 0;
        if (frame < 2)
            intOffset = -2;
        const int intro = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[1792]);
        bool intRuns = false;
        for (auto script = CTheScripts::pActiveScripts; script && !intRuns; script = script->m_pNext)
            intRuns = _strnicmp(script->m_szName, "INT", 8) == 0;
        if ((intro == 0 || intro == 1) && !intRuns && CTheScripts::pIdleScripts && GetTickCount() - intLastStart >= 5000)
        {
            if (intOffset == -2)
            {
                intOffset = -1;
                static const unsigned char signature[] = {0xA4, 0x03, 0x09};
                const unsigned int size = CTheScripts::MainScriptSize;
                int matches = 0;
                if (size >= sizeof(signature) + 8 && size <= 200000)
                    for (unsigned int i = 0; i <= size - sizeof(signature) - 8; ++i)
                        if (memcmp(&CTheScripts::ScriptSpace[i], signature, sizeof(signature)) == 0 &&
                            _strnicmp(reinterpret_cast<const char*>(&CTheScripts::ScriptSpace[i + sizeof(signature)]), "INT", 8) == 0)
                        {
                            intOffset = static_cast<int>(i);
                            ++matches;
                        }
                if (matches != 1)
                    intOffset = -1;
            }
            intLastStart = GetTickCount();
            if (intOffset >= 0)
            {
                CTheScripts::StartNewScript(reinterpret_cast<unsigned char*>(&CTheScripts::ScriptSpace[intOffset]));
                CMissionMirror::LogEventf("guest-launcher start name=INT offset=%d counter=%d", intOffset, intro);
            }
        }
    }

    CNetworkPlayer* host = nullptr;
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (networkPlayer && networkPlayer->m_bIsHost)
        {
            host = networkPlayer;
            break;
        }
    }
    if (!host && !authority)
        return;
    auto hostProgress = [&](int chain) {
        return chain == 0 ? host->m_nSweetProgress : static_cast<int>(host->m_anChainProgress[chain]);
    };
    for (auto& launcher : launchers)
    {
        int& counter = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[launcher.counterOffset]);
        if (authority)
        {
            // Sweet, Ryder, Smoke and Strap run from the intro to their chain's end: one gone mid-chain was lost by a save
            // made before the 14-11 fix (see GenericSave_OwnProgress). Never raised: his progress is his own.
            if (launcher.chain > 3 || counter <= 0 || counter >= launcher.endCounter)
                continue;
        }
        else
        {
        const int hostCounter = hostProgress(launcher.chain);
        const int prerequisite = hostProgress(launcher.prerequisiteChain);
        // the host's own launcher alive says it all; the counters only guess (a save had Ryder with Sweet at 2, 12.11)
        const bool hostRuns = (host->m_nStoryLaunchers & (1u << launcher.chain)) != 0;
        if (hostCounter < 0 || hostCounter == CMissionMode::CHAIN_UNKNOWN || hostCounter > launcher.endCounter ||
            (!hostRuns && (prerequisite < 0 || prerequisite == CMissionMode::CHAIN_UNKNOWN ||
                prerequisite < launcher.prerequisiteCounter)))
            continue;
        // Preserve SWEET's existing completed-chain behavior.
        if (launcher.chain == 0 && hostCounter >= launcher.endCounter)
            continue;
        if (counter < 0 || counter > launcher.endCounter)
            continue;
        if (counter < hostCounter)
        {
            CMissionMirror::LogEventf("guest-launcher counter name=%s from=%d to=%d", launcher.name, counter, hostCounter);
            if (launcher.ownCounter < 0 || counter != launcher.copiedCounter)
                launcher.ownCounter = counter;
            counter = launcher.copiedCounter = hostCounter;
            if (launcher.chain == 0)
                CMissionMode::UpdateSweetProgress(counter);
        }
        // CAT offers Catalina's dates again (mission 40) only once King in Exile (CATCUT) set $flag_trailor_cutscene
        // (8652): past her second mission the host has seen it. Only on a counter taken from the host.
        int& trailerCut = *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[8652]);
        if (launcher.chain == 7 && counter >= 2 && counter == launcher.copiedCounter && trailerCut == 0)
        {
            trailerCut = 1;
            CMissionMirror::LogEventf("guest-launcher trailer-cut name=%s counter=%d", launcher.name, counter);
        }
        if (counter >= launcher.endCounter)
            continue;
        }
        bool exists = false;
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        {
            if (_strnicmp(script->m_szName, launcher.name, 8) == 0)
            {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        if (!launcher.searched)
        {
            launcher.searched = true;
            // Original main.scm: 03A4, short string (09), 8 bytes.
            static const unsigned char signature[] = {0xA4, 0x03, 0x09};
            const unsigned int size = CTheScripts::MainScriptSize;
            int matches = 0;
            // ScriptSpace is a pointer in the SDK; its backing array has 200000 bytes.
            if (size >= sizeof(signature) + 8 && size <= 200000)
            {
                for (unsigned int i = 0; i <= size - sizeof(signature) - 8; ++i)
                {
                    if (memcmp(&CTheScripts::ScriptSpace[i], signature, sizeof(signature)) == 0 &&
                        _strnicmp(reinterpret_cast<const char*>(&CTheScripts::ScriptSpace[i + sizeof(signature)]), launcher.name, 8) == 0)
                    {
                        launcher.offset = static_cast<int>(i);
                        ++matches;
                    }
                }
            }
            if (matches != 1)
            {
                launcher.offset = -1;
                CMissionMirror::LogEventf("guest-launcher label-unavailable name=%s matches=%d", launcher.name, matches);
            }
            // CAT's label is 7 bytes before its SCRIPT_NAME: $load_and_launch_catalina_mission (2872) = -1. Skipping it
            // the switch sees 0 and launches Local Liquor Store at once.
            static const unsigned char catPrelude[] = {0x04, 0x00, 0x02, 0x38, 0x0B, 0x04, 0xFF};
            if (launcher.offset >= 7 && !_strnicmp(launcher.name, "CAT", 8) &&
                memcmp(&CTheScripts::ScriptSpace[launcher.offset - 7], catPrelude, sizeof(catPrelude)) == 0)
                launcher.offset -= 7;
        }
        // Loaded saves may lack the thread; new threads get their name on their first command.
        if (launcher.offset < 0 || !CTheScripts::pIdleScripts || GetTickCount() - launcher.lastStart < 5000)
            continue;
        // StartNewScript dereferences pIdleScripts (gta-reversed TheScripts.cpp, 0x464C20).
        launcher.started = CTheScripts::StartNewScript(reinterpret_cast<unsigned char*>(&CTheScripts::ScriptSpace[launcher.offset]));
        launcher.lastStart = GetTickCount();
        CMissionMirror::LogEventf("guest-launcher start name=%s offset=%d counter=%d authority=%d", launcher.name, launcher.offset, counter,
            authority ? 1 : 0);
    }
}

// T98 (mission-host-driver-hint): a guest at the wheel of a vehicle the mission made, where the mission needs the host
// himself in it (requiresHostCar or one of its excluded sections), is told so in his help box
static void ProcessHostDriverHint()
{
    if (!CConfigLoader::ms_bMissionHostDriverHint || !CMissionSyncRules::IsEnabled() || !CNetwork::m_bConnected ||
        COpCodeSync::bProcessingNetworkOpcode || !CCoopMission::IsScriptAuthority() || !FindPlayerPed(0))
        return;
    CRunningScript* mission = nullptr;
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext)
        if (script->m_bIsMission && CMissionSyncRules::IsStoryScript(script) && COpCodeSync::IsScriptSynced(script))
            mission = script;
    if (!mission || !IsHostCarRequired(mission, mission->m_pCurrentIP))
        return;
    static std::unordered_map<uint32_t, DWORD> lastHint;
    const DWORD now = GetTickCount();
    for (auto guest : CNetworkPlayerManager::m_pPlayers)
    {
        CPed* ped = guest->m_pPed;
        if (!ped || guest->m_bDead || ped->m_fHealth <= 0.0f || !CCoopMission::ShouldIncludeNetworkPlayer(guest) ||
            !ped->m_nPedFlags.bInVehicle || !ped->m_pVehicle || ped->m_pVehicle->m_pDriver != ped ||
            !IsMissionCleanupVehicle(ped->m_pVehicle) || CCommandPutNetworkPlayersInCar::IsParkedCopy(ped->m_pVehicle))
            continue;
        DWORD& last = lastHint[guest->m_playerId.value()];
        if (last && now - last < 6000)
            continue;
        last = now;
        Packets::Scripts::MissionScriptedExit packet{};
        packet.playerid = guest->m_playerId;
        packet.opcode = CMissionSyncRules::HOST_DRIVER_HINT_OPCODE;
        GetPacketFactory().Send(packet);
        CMissionMirror::LogEventf("rule-host-driver-hint script=%.8s ip=%d player=%u model=%d", mission->m_szName,
            (int)(mission->m_pCurrentIP - mission->m_pBaseIP), guest->m_playerId.value(), ped->m_pVehicle->m_nModelIndex);
    }
}

// T98 (music1-garage-host-driver): MUSIC1_11889 drives the van into the garage with a sequence on $scplayer
// (TASK_CAR_DRIVE_TO_COORD, MUSIC1_12313) and only warps him in when he is on foot (MUSIC1_12103). With the host as a
// passenger and a guest at the wheel the task never finishes and the scene hangs until it is skipped. At OPEN_GARAGE
// (0360, MUSIC1_11920, during the fade) the guest is taken out and the host is put at the wheel.
static void OnMusic1GarageOpen(CRunningScript* script)
{
    if (!CConfigLoader::ms_bMusic1GarageHostDriver || !CConfigLoader::ms_bVanillaScripts || !script ||
        !script->m_bIsMission || _strnicmp(script->m_szName, "MUSIC1", 8) != 0 || COpCodeSync::bProcessingNetworkOpcode ||
        !CNetwork::m_bConnected || !CCoopMission::IsScriptAuthority() || !COpCodeSync::IsScriptSynced(script))
        return;
    CPlayerPed* host = FindPlayerPed(0);
    if (!host || host->m_fHealth <= 0.0f || !host->m_nPedFlags.bInVehicle || !host->m_pVehicle)
        return;
    CVehicle* van = host->m_pVehicle;
    CNetworkPlayer* driver = van->m_pDriver ? CNetworkPlayerManager::GetPlayer(van->m_pDriver) : nullptr;
    if (van->m_pDriver == host || !driver || van->m_nModelIndex != 413 || !IsMissionCleanupVehicle(van))
        return;
    Packets::Scripts::MissionScriptedExit packet{};
    packet.playerid = driver->m_playerId;
    packet.opcode = 0x0362;  // WARP_CHAR_FROM_CAR_TO_COORD on the guest (his handler flushes him out)
    GetPacketFactory().Send(packet);
    // the copy leaves the seat here now; its late driver updates do not take it back (IsSeatHeldLocally)
    driver->RemoveFromVehicle(van);
    const int hostRef = CPools::GetPedRef(host);
    const CVector pos = host->GetPosition();
    Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(hostRef, pos.x, pos.y, pos.z + 1.0f);
    Command<Commands::WARP_CHAR_INTO_CAR>(hostRef, CPools::GetVehicleRef(van));
    CMissionMirror::LogEventf("rule-music1-garage-host-driver player=%u driver-now=%d", driver->m_playerId.value(),
        van->m_pDriver == host ? 1 : 0);
}

void CMissionSyncRules::Process()
{
    ProcessGuestStoryLaunchers();
    ProcessHostDriverHint();
    if (!COpCodeSync::bProcessingNetworkOpcode)
        TrackHoodInterior();
    ProcessAllyFollowArea();
    ProcessEntryWindow();
    ProcessGuestCar();
    CCommandTeleportPlayersToHostSafely::ProcessPendingTeleport();
    CCommandPutNetworkPlayersInCar::ProcessPendingBoarding();
    ProcessDriveByGuestCars();
    ProcessLosSepulcrosGuestCars();
    ProcessManagementPier();
    ProcessRobbingUncleSamGuestCars();
    ProcessHomeInvasionGuestCars();
    ProcessDriveThruGuestCars();
    ProcessCatalystGuestCars();
    ProcessRunningDogGuestCars();
    ProcessWrongSideTracksGuestCars();
    ProcessBeachArrival();
    ProcessJustBusinessGuestCars();
    ProcessCountrysideGuestVehicles();
    CJustBusinessEscort::Process();
    ProcessTripSkip();
    ProcessMissionBoarding();
    ProcessOgLocGuestCar();
    CCommandPutNetworkPlayersInCar::ProcessGuestVehicles();
    if (!IsEnabled())
        return;

    if (!bRegistered)
        RegisterSyncedScripts();

    if (!IsStoryMissionRunning())
    {
        bDriveThruGuestCarTriggered = false;
        bOgLocGuestCarTriggered = false;
        bOgLocBikesTriggered = false;
        ogLocCarRef = -1;
        CCommandPutNetworkPlayersInCar::ResetMissionGuestCar();
    }
    CCommandPutNetworkPlayersInCar::ProcessParkedCopies();
    if (nObjectiveVehicles > 0 && !IsStoryMissionRunning())
        nObjectiveVehicles = 0;

    if (!bTeleportPending)
        return;

    const DWORD now = GetTickCount();
    if (now - nLastTeleport < MIN_TELEPORT_INTERVAL_MS)
        return;  // stays pending: once more after the interval, from where the host ended up
    bTeleportPending = false;
    nLastTeleport = now;

    if (!CCoopMission::IsScriptAuthority() || !FindPlayerPed(0))
        return;

    CNetworkPlayer* networkPlayers[MAX_TELEPORT_SLOTS];
    int playerCount = 0, nearCount = 0;
    const float skipNear = fGatherSkipNear;
    fGatherSkipNear = 0.0f;
    CPlayerPed* host = FindPlayerPed(0);
    for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (playerCount < MAX_TELEPORT_SLOTS && networkPlayer->m_pPed && CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
        {
            CPed* ped = networkPlayer->m_pPed;
            const CVector delta = ped->GetPosition() - host->GetPosition();
            if (skipNear > 0.0f && ped->m_nAreaCode == host->m_nAreaCode &&
                delta.x * delta.x + delta.y * delta.y <= skipNear * skipNear)
            {
                ++nearCount;
                continue;
            }
            networkPlayers[playerCount++] = networkPlayer;
        }
    }
    CCommandTeleportPlayersToHostSafely::TeleportToHost(networkPlayers, playerCount);
    CMissionMirror::LogEventf("rule-teleport players=%d near-kept=%d", playerCount, nearCount);
}
