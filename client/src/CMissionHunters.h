#pragma once

// Mission enemies sent after a player go after the nearest player of the mission instead.
// A script sends its enemies after $PLAYER_ACTOR (TASK_KILL_CHAR_ON_FOOT 05E2), which on the script authority is only
// his own ped, so the guests were never hunted. On the client that runs a mission ped (m_bSyncing), every 500 ms a ped
// in CTaskComplexKillPedOnFoot (1000) whose target is a player switches, through the same command, to a player of the
// mission that is clearly nearer (under 40 m and under 70% of the current target's distance, same interior). Its shots
// already reach that player through the ped sync. A hostile mission ped (hates PED_TYPE_PLAYER1) that is not attacking
// yet goes after a guest within 25 m, in sight and clearly nearer than the local player (before, only after he shot it).
class CMissionHunters
{
public:
    static void Process();
};
