#pragma once
#include <cstdint>
class CPed;
class CVector;
// T75: "New Model Army" (ZERO4) for the guest: his own RC Goblin over the war table (interior area 10) with a magnet
// of its own. The fire button lifts the copy of a barrel or plank under the toy and drops it; where the copy hangs
// goes to the mission owner by CMissionObjectMoves, so the real one follows and falls where the guest drops it: the
// mission's own checks (planks on the gap, barrels on the tanks or in the way) count natively, once.
// Adapted from Karimov-Akbar/sa-coop (MIT), features/story/rc-toys/RcToys.cpp (CJ at the table, the floor on the
// way back) and features/mission-objects/Objects.cpp (the partner's magnet); see NOTICE.md.
class CMissionWarTable
{
public:
    static void Reset();    // session end: nothing held, CJ back in the street
    static void Process();  // once a frame, after CMissionMachines::Process
    // CMissionMachines (GiveToy): a toy of the table is coming, CJ goes into its area meanwhile (he is frozen already)
    static void Enter(CPed* me, const CVector& table, uint8_t area);
};
