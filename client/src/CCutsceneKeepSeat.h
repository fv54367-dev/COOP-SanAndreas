#pragma once

// cutscene-keep-seat: the authority's LOAD_CUTSCENE replayed here runs the game's own "warp the player out of his vehicle"
// (gta-reversed CutsceneMgr.cpp LoadCutsceneData, info section "offset": CTaskSimpleCarSetPedOut). A guest that was sitting
// in a car (his own or somebody else's, driver or passenger) is put back in the same seat when the scene is over, if the
// car is still there, near and that seat is free (2026-11-15, High Stakes: guests were left on foot after Cesar's scene).
class CCutsceneKeepSeat
{
public:
    static void BeforeNetworkLoad();
    static void Process();
};
