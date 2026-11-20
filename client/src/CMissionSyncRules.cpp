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
#include "CCatalystRyderCar.h"
#include "CMissionRulesCompare.h"
#include "CMissionRulesOpcodes.h"
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

using namespace mission_guest_cars;
using namespace mission_compare;
using namespace mission_opcodes;

static constexpr DWORD MIN_TELEPORT_INTERVAL_MS = 500;
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
static DWORD nLastTeleport = 0;
// T97: a pending gather leaves out the players already this near the host (0 = everybody, as before)
// T97 MUSIC1: when the arrival scene at the beach party started (its ground snap of the host follows in the same tick)


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

void CMissionSyncRules::InjectHooks()
{
    InjectCompareHook();
    // the save of C_PcSave::SaveSlot: call GenericSave (E8 to 0x5D13E0), only with those bytes there
    const uint8_t saveCall[] = {0xE8, 0x5A, 0x83, 0xFB, 0xFF};
    if (memcmp(reinterpret_cast<const void*>(0x619081), saveCall, sizeof(saveCall)) == 0)
        patch::RedirectCall(0x619081, CMissionRulesCounters::SaveOwnProgress);
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


void CMissionSyncRules::Process()
{
    CMissionRulesCounters::Process();
    ProcessHostDriverHint();
    if (!COpCodeSync::bProcessingNetworkOpcode)
        TrackHoodInterior();
    ProcessAllyFollowArea();
    ProcessEntryWindow();
    ProcessGuestCar();
    CCommandTeleportPlayersToHostSafely::ProcessPendingTeleport();
    CCommandPutNetworkPlayersInCar::ProcessPendingBoarding();
    ProcessDriveByGuestCars(bTeleportPending);
    ProcessLosSepulcrosGuestCars(bTeleportPending);
    ProcessManagementPier();
    ProcessRobbingUncleSamGuestCars();
    ProcessHomeInvasionGuestCars();
    ProcessDriveThruGuestCars();
    ProcessCatalystGuestCars();
    ProcessCatalystRyderCar();
    ProcessRunningDogGuestCars(bTeleportPending);
    ProcessWrongSideTracksGuestCars(bTeleportPending);
    ProcessBeachArrival();
    ProcessJustBusinessGuestCars(bTeleportPending);
    ProcessCountrysideGuestVehicles();
    CJustBusinessEscort::Process();
    ProcessTripSkip();
    ProcessMissionBoarding(bTeleportPending);
    ProcessOgLocGuestCar();
    CCommandPutNetworkPlayersInCar::ProcessGuestVehicles();
    if (!IsEnabled())
        return;

    if (!bRegistered)
        RegisterSyncedScripts();

    if (!IsStoryMissionRunning())
    {
        ResetGuestCarTriggers();
        CCommandPutNetworkPlayersInCar::ResetMissionGuestCar();
    }
    CCommandPutNetworkPlayersInCar::ProcessParkedCopies();
    ResetObjectivesIfIdle();

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
