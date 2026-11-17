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


namespace mission_opcodes {
bool bTeleportPending = false;
float fGatherSkipNear = 0.0f;
}
using namespace mission_guest_cars;
using namespace mission_compare;
using namespace mission_opcodes;

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
void mission_opcodes::ProcessManagementPier()
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

static CRunningScript* pHoodScript = nullptr;
static int nHoodMission = -1;
static bool bHoodEntered = false;
static bool bHoodDoorSendPending = false;
static bool bHoodDoorReopened = false;

CPlayerPed* mission_opcodes::TrackHoodInterior()
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

