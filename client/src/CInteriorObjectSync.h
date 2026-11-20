#pragma once

#include "network/packets/peds.h"
#include <cstdint>

class CPed;
class CObject;
class CNetworkPed;
class CRunningScript;

// interior-object-sync + interior-service-guest (TANDA 2026-11-18, critique-adjusted):
// part 1: the interior owner publishes objects held by its interior peds (the diners' pizza, the keeper's
//         tray); guests keep visual copies tracked to their ped copy's hand bone every frame with the game's
//         bone functions (no hold task on copies: plugin-sdk's CTaskSimpleHoldEntity is an empty stub and its
//         destructor/abort would delete or drop the copy behind our back).
// part 2: a guest being served forwards its hidden local keeper's pose + tray to the interior owner, who
//         applies them to the real keeper; the owner's normal interior sync shows the sequence to everyone.
//         Native script activity (observed opcode stream) wins; 1.5 s without an update releases the override.
class CInteriorObjectSync
{
public:
    static void Process();      // 100 ms: publish (owner), refresh overrides, send service poses + maintain copies (guest)
    static void FrameUpdate();  // every frame: track held copies / override objects to the hand bone
    static void HandleHeldObject(Packets::Peds::InteriorHeldObject* packet);    // guest
    static void HandleServicePose(Packets::Peds::InteriorServicePose* packet);  // owner
    static void ObserveNativeOpcode(CRunningScript* script, uint16_t opcode);   // owner: native script activity
    static void OnRoleChanged();             // CInteriorOwnership: owner <-> guest <-> none
    static void OnPedDestroyed(CNetworkPed* record);  // before the game ped goes away
    static void Reset();                     // disconnect / session end
};
