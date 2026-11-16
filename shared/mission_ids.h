#pragma once

#include <cstring>

// Mission ids of `DEFINE MISSION` in scm/main.txt. The mission lock (docs/REQUISITOS.md 1) only covers story missions:
// side missions, minigames and schools can be played by several players at once, even the same one.
inline bool IsSideMissionId(int missionId)
{
    return (missionId >= 3 && missionId <= 9)  // video games, pool, lowrider (bet and dance)
        || missionId == 35                     // race tournament / 8-track / dirt track
        || missionId == 71                     // Back to School
        || missionId == 83                     // Learning to Fly
        || (missionId >= 113 && missionId <= 134);  // shooting range, gyms, trucking .. sub-missions, courier, stunts
}

// the race tournament mission (CPRACE: street races, 8-track, dirt track). Several players can run it at the same time, each
// in his own game: that is a VS battle (CVsRace)
inline bool IsRaceMissionId(int missionId)
{
    return missionId == 35;
}

// docs/MISIONES_MODO.md: story missions that are only played alone (any player alone in the marker starts them)
inline bool IsIndividualOnlyMissionId(int missionId, bool coopLowrider = false)
{
    return missionId == 19 && !coopLowrider;  // Cesar Vialpando
}

// the side-mission scripts (Coop.ChooseSideMissionMode) that are only played alone: taxi, pimping, the driving, bike, boat
// and flying schools, and the races (each player runs his own: several at once are a VS battle, CVsRace)
inline bool IsIndividualOnlyScript(const char* szScriptName)
{
    static const char* const aszNames[] = {"TAXIODD", "PIMP", "DSKOOL", "BSKOOL", "BOAT", "DESERT5", "CPRACE"};
    for (const char* szName : aszNames)
    {
        if (szScriptName && strncmp(szScriptName, szName, 7) == 0)
            return true;
    }
    return false;
}
