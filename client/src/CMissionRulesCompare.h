#pragma once
#include <cstdint>
#include <windows.h>

class CRunningScript;

namespace mission_compare {

constexpr int MAX_TELEPORT_SLOTS = 3;

void InjectCompareHook();
void ProcessAllyFollowArea();
void ProcessEntryWindow();
void ProcessBeachArrival();
void ProcessGuestCar();
bool IsHostCarRequired(CRunningScript* script, const uint8_t* ip);

// Shared only with the opcode observer; owned by the comparison module.
extern DWORD nBeachArrivalTick;

}  // namespace mission_compare
