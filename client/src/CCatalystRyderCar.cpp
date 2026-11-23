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

// the guest that takes the wheel of Ryder's Picador: the first included one, alive
CNetworkPlayer* FirstGuest()
{
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p != nullptr && p->m_pPed != nullptr && p->m_pPed->m_fHealth > 0.0f &&
            CCoopMission::ShouldIncludeNetworkPlayer(p))
            return p;
    return nullptr;
}

bool IsPlayerPed(CPed* ped)
{
    if (ped == nullptr) return false;
    if (ped == FindPlayerPed(0)) return true;
    for (auto p : CNetworkPlayerManager::m_pPlayers)
        if (p != nullptr && p->m_pPed == ped) return true;
    return false;
}

// The script seats a Grove goon in Ryder's Picador (ryder3.sc:1799-1805, WARP_CHAR_INTO_CAR) and he drives it. The owner
// asked for that seat to be the guest's (20-11). He is taken OUT of the car ALIVE, never DELETE_CHAR: the script recreates
// him seated if it sees him dead in the same block (:1810-1812) and still uses him later (:2278, :2365).
CPed* GoonDriving(CVehicle* car)
{
    CPed* driver = car != nullptr ? car->m_pDriver : nullptr;
    if (driver == nullptr || !driver->IsVTableValid() || driver->m_fHealth <= 0.0f || IsPlayerPed(driver) ||
        driver->m_nCreatedBy != MISSION_CHAR)
        return nullptr;
    return driver;
}

void MoveOutOfCar(CPed* ped, CVehicle* car)
{
    // two metres to the car's right, at the car's own ground: he steps aside instead of vanishing across the map
    CVector at = car->GetPosition();
    if (car->m_matrix != nullptr)
    {
        CVector right = car->m_matrix->right;
        at += right * 2.5f;
    }
    Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(ped), at.x, at.y, at.z + 1.0f);
    CMissionMirror::LogEventf("catalyst-ryder-car goon moved ped=%d model=%d car=%d", CPools::GetPedRef(ped),
        ped->m_nModelIndex, CPools::GetVehicleRef(car));
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
    static bool stopped = false, parked = false;
    static uint32_t lastRequest = 0, lastState = 0, stoppedAt = 0;
    CRunningScript* script = Running();
    if (script != attempt)
    {
        attempt = script;
        stopped = parked = false;
        lastRequest = lastState = stoppedAt = 0;
    }
    if (!script) return;

    if (L(89) < 10) return;  // the catch stage: the owner's CJ is on the caboose, Ryder on the car

    CVehicle* car = CPools::GetVehicle(L(108));
    if (!car || !car->IsVTableValid() || car->m_nModelIndex != 600 || car->m_fHealth <= 0.0f) return;

    if (!stopped)
    {
        stopped = true;
        stoppedAt = GetTickCount();
        plugin::Command<Commands::STOP_PLAYBACK_RECORDED_CAR>(L(108));
        CMissionMirror::LogEventf("catalyst-ryder-car handoff car=%d guests=%d", L(108), GuestCount());
    }

    CNetworkPlayer* guest = FirstGuest();
    const bool guestDrives = guest != nullptr && guest->m_pPed != nullptr && car->m_pDriver == guest->m_pPed;

    // the goon leaves the driver's seat (checked every frame, so a script that seats him again gets corrected)
    if (!guestDrives)
        if (CPed* goon = GoonDriving(car)) MoveOutOfCar(goon, car);

    // and the guest is asked to take it. Not a one-shot latch: the request repeats while the seat is still not his, so a
    // late rejection (his copy still sitting in the car, area change, the 8 s window) does not leave the car parked
    const uint32_t now = GetTickCount();
    if (guest != nullptr && !guestDrives && now - lastRequest > 2000 && !guest->m_pPed->m_nPedFlags.bInVehicle)
    {
        lastRequest = now;
        CCommandPutNetworkPlayersInCar::QueueGuestPassenger(guest->m_playerId, car,
            CCommandPutNetworkPlayersInCar::GUEST_DRIVER_SEAT, script, &CConfigLoader::ms_bMissionCatalystRyderCar);
        CMissionMirror::LogEventf("catalyst-ryder-car boarding guest=%u seat=driver car=%d", guest->m_playerId.value(),
            L(108));
    }

    if (now - lastState > 5000)
    {
        lastState = now;
        CMissionMirror::LogEventf("catalyst-ryder-car state driver=%s guest=%d goon=%d",
            guestDrives ? "guest" : "none", guest ? (int)guest->m_playerId.value() : -1, GoonDriving(car) ? 1 : 0);
    }

    // The extra Picadores, once the guest is really at the wheel (then he is not counted as a guest left on foot) or after
    // a wait, so a guest that cannot take the car does not leave the others without one. One per TWO guests: a Picador
    // seats the driver plus one (owner's rule 21-11)
    const int others = GuestCount() - 1;
    if (others < 1 || parked || (!guestDrives && now - stoppedAt < 15000)) return;
    parked = true;
    CCommandPutNetworkPlayersInCar::ParkSharedGuestCar(car, script, "catalyst", (std::min)(others, 3), false,
        CConfigLoader::ms_bMissionGuestVehiclesSelfBoard ? CCommandPutNetworkPlayersInCar::GUEST_PARK_ONLY
                                                         : CCommandPutNetworkPlayersInCar::GUEST_PARK_ALL);
    CMissionMirror::LogEventf("catalyst-ryder-car extra Picador for %d guests", others);
}
