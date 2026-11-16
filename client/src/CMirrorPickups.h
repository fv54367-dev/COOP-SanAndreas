#pragma once

#include <cstdint>

class CRunningScript;
namespace Packets::Mirror
{
class MirrorPickup;
}

// The mission mirror layer's pickups (idea from SACoop, written from scratch; gta-reversed Pickups.cpp).
// Authority (CCoopMission::IsScriptAuthority, coop mode): a pickup its mission family creates (CREATE_PICKUP 0213,
// CREATE_PICKUP_WITH_AMMO 032B; the handle left in ScriptParams[0]) is sent to the others, and its end (collected by
// the authority, REMOVE_PICKUP, mission cleanup) too. A guest's OP_COLLECTED marks the authority's pickup collected
// the native way: CPickups::AddToCollectedPickupsArray (0x455240, the ring HAS_PICKUP_BEEN_COLLECTED 0214 reads through
// IsPickUpPickedUp 0x454B40), then the pickup goes like a collected one-shot pickup.
// Receivers in coop: a local copy (CPickups::GenerateNewOne); collecting it gives the native reward here and sends
// OP_COLLECTED. Individual mode (F1) or out of the mission: no copies. Switch: `mirror-pickups`.
class CMirrorPickups
{
public:
    static bool IsEnabled();

    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame
    static void Process();
    // MISSION_MIRROR_PICKUP from the server
    static void HandlePacket(const Packets::Mirror::MirrorPickup& packet);
};
