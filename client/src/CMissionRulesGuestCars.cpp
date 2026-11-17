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

namespace mission_guest_cars {

bool IsMissionCleanupVehicle(CVehicle* vehicle)
{
    if (!vehicle)
        return false;
    const int handle = CPools::GetVehicleRef(vehicle);
    for (const auto& entry : CTheScripts::MissionCleanUp.m_Objects)
        if (entry.type == MISSION_CLEANUP_ENTITY_TYPE_VEHICLE && entry.handle == handle)
            return true;
    return false;
}

static bool bDriveThruGuestCarTriggered = false;
static bool bOgLocGuestCarTriggered = false;
static bool bOgLocBikesTriggered = false;
static int ogLocCarRef = -1;
static CRunningScript* ogLocCarScript = nullptr;
static DWORD ogLocCarSince = 0;
static DWORD ogLocHeadingSince = 0;
static float ogLocHeading = 0.0f;

void ProcessOgLocGuestCar()
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

void OnOgLocPassengerWarp(CRunningScript* script)
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


void OnDriveThruCarHeadingSet(CRunningScript* script)
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

// a synced story mission runs here (this game is its authority)
bool IsStoryMissionRunning()
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

void OnOgLocBikeHeadingSet(CRunningScript* pScript)
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
void OnCarBlipAdded(CRunningScript* pScript)
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
void ProcessCountrysideGuestVehicles()
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
void ProcessDriveByGuestCars(bool& bTeleportPending)
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
void ProcessLosSepulcrosGuestCars(bool& bTeleportPending)
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
void ProcessRobbingUncleSamGuestCars()
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
void ProcessHomeInvasionGuestCars()
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
void ProcessDriveThruGuestCars()
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
void ProcessCatalystGuestCars()
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

void ProcessRunningDogGuestCars(bool& bTeleportPending)
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

void ProcessWrongSideTracksGuestCars(bool& bTeleportPending)
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

void ProcessJustBusinessGuestCars(bool& bTeleportPending)
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
void ProcessTripSkip()
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

void ProcessMissionBoarding(bool& bTeleportPending)
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


void ResetGuestCarTriggers()
{
        bDriveThruGuestCarTriggered = false;
        bOgLocGuestCarTriggered = false;
        bOgLocBikesTriggered = false;
        ogLocCarRef = -1;
}

void ResetObjectivesIfIdle()
{
    if (nObjectiveVehicles > 0 && !IsStoryMissionRunning())
        nObjectiveVehicles = 0;
}

void OnMusic1GarageOpen(CRunningScript* script)
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

}  // namespace mission_guest_cars
