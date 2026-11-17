#pragma once

class CPlayerPed;

namespace mission_opcodes {

void ProcessManagementPier();
CPlayerPed* TrackHoodInterior();

// Runtime state shared with the lifecycle dispatcher; defined once in the opcode module.
extern bool bTeleportPending;
extern float fGatherSkipNear;

}  // namespace mission_opcodes
