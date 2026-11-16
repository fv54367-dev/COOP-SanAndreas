#pragma once

// Sends the parachute animation the local player's PLCHUTE script is playing and reproduces it (plus the open
// parachute object) on the other players' peds. The animation table lives in CParachuteSync.cpp.
class CParachuteSync
{
public:
    static void Process();
};
