#pragma once
#include <cstdint>
namespace Packets::Scripts { class MachinePhase; class GuestMachineDamage; }
// T67: the guest's own seat of the mission's machines (the turret of "Air Raid", the RC Baron of "Supply Lines..."),
// and the channel that makes what the guest's machine does count in the owner's script.
// Adapted from Karimov-Akbar/sa-coop (MIT), features/story/rc-toys/RcToys.cpp; see NOTICE.md.
class CMissionMachines
{
public:
    static void Init();     // CNetwork::ResetSessionState hook point: clean state between sessions
    static void Reset();    // the machines go away when the session ends
    static void Process();  // once a frame
    static void OnPhase(const Packets::Scripts::MachinePhase& p);
    static bool GuestToyPending();  // T75: our own toy is out, or its floor is awaited (CMissionWarTable)
    static void OnDamage(const Packets::Scripts::GuestMachineDamage& p);
};
