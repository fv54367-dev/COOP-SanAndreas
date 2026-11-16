#pragma once

#include <network/packets/scripts.h>
#include <vector>

// Story progress of a coop story mission. The host's script is the only one that runs it, so when it passes, the story
// flags it sets (e.g. Sweet's counter, which picks the next mission at his marker) only change in the host's game and the
// guests would start the same mission again. At the end of a PASSED mission the authority sends which small global vars
// (flags and counters, value 0..255) changed since the mission started; a receiver changes one only if it still has the
// old value, so it never overwrites a progress of his own, entity handles or timers. Everything runs on the game thread.
class CStoryProgress
{
public:
    // the authority's mission was launched: remember the global vars
    static void OnMissionLaunched();
    // register_mission_passed ran in the authority's mission
    static void OnMissionPassed();
    // the authority's mission script ended: send what changed if it was passed
    static void OnMissionEnded();
    // a receiver
    static void Apply(const Packets::Scripts::StoryProgressSync& packet);

private:
    // end of the global var area: main.scm starts with a goto past it (same on every client)
    static int GetGlobalsEnd();

    static inline std::vector<uint8_t> ms_base;
    static inline bool ms_bTracking = false;
    static inline bool ms_bPassed = false;
};
