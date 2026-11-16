#pragma once
#include <cstdint>
namespace Packets::Scripts { class GuestObjectMove; }
// T70: the reusable channel that makes a mission object a guest moves count in the owner's script. The guest
// claims his copy while his machine moves it (the copy becomes pushable so the machine can), reports where it
// stands, and the authority puts the real object there: the owner's mission checks (contacts, locations, health)
// then fire natively and count once. CObjectSync skips the objects claimed here.
class CMissionObjectMoves
{
public:
    static void Reset();                      // session/mission end: release everything
    static void Process();                    // release claims whose copy is gone or stopped being reported
    static bool IsClaimed(uint32_t objectid); // CObjectSync: the guest is moving it, his word stands
    // guest glue (all no-ops with the switch off, not connected or not a guest):
    static bool Claim(uint32_t objectid);     // the copy becomes pushable; true when newly claimed
    static void Report(uint32_t objectid);    // send the copy's transform (rate-limited, 100 ms)
    static void Unclaim(uint32_t objectid);   // final report; the copy goes back to the owner's sync
    static void Receive(const Packets::Scripts::GuestObjectMove& p);  // authority: apply to the real object
};
