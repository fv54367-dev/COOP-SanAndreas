#include "stdafx.h"
#include "CCatalystRyderCar.h"
#include "CConfigLoader.h"
#include "CMissionMirror.h"
#include "CMissionSyncRules.h"
#include "CCoopMission.h"
#include "COpCodeSync.h"
#include "Commands/Commands/CCommandPutNetworkPlayersInCar.h"
#include <algorithm>

namespace
{
int& L(int i) { return CTheScripts::LocalVariablesForCurrentMission[i].iParam; }

CRunningScript* Running()
{
    CRunningScript* found = nullptr;
    for (auto s = CTheScripts::pActiveScripts; s; s = s->m_pNext)
        if (s->m_bIsMission && !_strnicmp(s->m_szName, "RYDER3", 8) && COpCodeSync::IsScriptSynced(s)) found = s;
    return found;
}

int GuestCount()
{
    int n = 0;
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p->m_pPed && p->m_pPed->m_fHealth > 0.0f && CCoopMission::ShouldIncludeNetworkPlayer(p)) ++n;
    return n;
}
}

// The owner's script drives the Picador with the recorded route 486 (RYDER3.txt 1507-1549) while his CJ rides the caboose
// throwing crates at Ryder, who stands attached to that car. With this switch the recording is stopped at the catch stage
// (89@ 10) and a guest drives the car himself; one more Picador carries the remaining guests. The count still comes from
// Ryder catching the crates in the owner's game.
void ProcessCatalystRyderCar()
{
    if (!CConfigLoader::ms_bMissionCatalystRyderCar || !CConfigLoader::ms_bVanillaScripts ||
        !CMissionSyncRules::IsEnabled() || !CNetwork::m_bConnected || COpCodeSync::bProcessingNetworkOpcode ||
        !CCoopMission::IsScriptAuthority()) return;

    static CRunningScript* attempt = nullptr;
    static bool handed = false, parked = false;
    CRunningScript* script = Running();
    if (script != attempt) attempt = script, handed = parked = false;
    if (!script || handed) return;

    if (L(89) < 10) return;  // the catch stage: the owner's CJ is on the caboose, Ryder on the car

    CVehicle* car = CPools::GetVehicle(L(108));
    if (!car || !car->IsVTableValid() || car->m_nModelIndex != 600 || car->m_fHealth <= 0.0f) return;

    handed = true;
    plugin::Command<Commands::STOP_PLAYBACK_RECORDED_CAR>(L(108));
    const int guests = GuestCount();
    CMissionMirror::LogEventf("catalyst-ryder-car handoff car=%d guests=%d", L(108), guests);

    // ONE more Picador beside Ryder's for the rest of the guests (they board it themselves, as the other guest cars)
    const int others = guests - 1;
    if (others < 1 || parked) return;
    parked = true;
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, script, "catalyst", (std::min)(others, 3), false,
        CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY
                                                         : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
    CMissionMirror::LogEventf("catalyst-ryder-car extra Picador for %d guests", others);
}
