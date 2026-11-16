#pragma once

#include <cstdint>

class CRunningScript;
class CVehicle;

// Just Business (DRUGS1, original scripts) with guests: Smoke rides a recorded bike (Car.StartPlayback 250..268 on his
// BF-400) with CJ behind him. Each guest gets a bike of his own with a Grove Street rider, the guest behind him, that
// plays the same recordings as Smoke's bike `n` seconds later (1 s for the first guest, 2 s the second, 3 s the third):
// every start / stop / speed / skip of Smoke's bike is queued for each escort and played back when its time comes, so
// all of them go the same way a little behind. Switch `just-business-bike-escort` (off: a bike of his own to drive,
// parked next to Smoke's, as before).
class CJustBusinessEscort
{
public:
    // the authority's script placed Smoke's bike (SET_CAR_HEADING of the first 581 at the start); nothing is created yet
    static void OnSmokeBike(CRunningScript* script, CVehicle* bike);
    // every command the script ran (CMissionSyncRules::OnCommandExecuted): the playback ones of Smoke's bike
    static void OnCommandExecuted(CRunningScript* script, uint16_t opcode);
    // every frame: the queued events that are due, the end of the mission
    static void Process();
};
