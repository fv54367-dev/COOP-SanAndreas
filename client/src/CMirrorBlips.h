#pragma once
// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0), package M2, and adapted.

#include <cstdint>
#include <cstdio>
#include <network/ids.h>

class CRunningScript;
namespace Packets::Mirror
{
class MirrorBlips;
}

// The mission mirror layer's blips (project_gta_sa_coop docs/design/mirror-layer.md 3.5, 3.10 and 3.11, package M2;
// decision M-D1). The blips the script authority's mission creates reach the other players with every change, with no
// lines in the mission scripts. The rules are in CMirrorBlipsPolicy.h. Switch: `mirror-blips` (CConfigLoader); off =
// nothing here runs and the Coop.*Blip* commands send their packets as before.
//
// Authority ("host" in the source; CCoopMission::IsScriptAuthority): a creating blip command (add_blip_for_char,
// add_blip_for_coord, ...) run by its mission family (CMissionMirror::IsFamilyScript) makes the handle it left in
// ScriptParams[0] an owned blip. Once per script frame the module reads the owned radar traces and sends what changed
// (MISSION_MIRROR_BLIPS): the whole state of a new or changed blip, its removal, and the position of a blip the others
// show at the authority's position. Owned blips stay mirrored after the mission ends for as long as the authority keeps
// them. A joiner (or a player back to coop) gets them all (bReset, to that player alone); the authority's restart or
// load resets them on every other game. The host's static blip snapshot (CNetworkStaticBlip) leaves owned traces out.
// Receivers: an entity blip on their copy of a network entity, found by its network id; a radar-only coordinate blip at
// the authority's position while it isn't found (and for objects, pickups, searchlights and trains), switched to the
// entity blip once it is; the same coordinate or contact point blip as the authority's, with its interior entrance; no
// blip on their own player. A receiver in individual mode (F1) or that left the mission shows none. Everything mirrored
// goes when the authority changes or the session ends; their static blip snapshot (CNetworkStaticBlip::Create) leaves
// the traces this module made.
// Everything runs on the game thread except WriteDiagSummary.
class CMirrorBlips
{
public:
    // the `mirror-blips` switch
    static bool IsEnabled();
    // a mission whose script gives each player his own blips (Coop.*BlipForNetworkPlayer: the car blip that goes for
    // the one inside it, Ryder's car for the third player...) runs here: its blips are not mirrored and the Coop.*Blip
    // commands keep working as before for all of its blips
    static bool IsPerPlayerMissionRunning();
    // the Coop.*Blip* commands send nothing: the mirror layer carries the blips of this mission
    static bool MutesCoopBlipCommands();

    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame
    static void Process();
    // MISSION_MIRROR_BLIPS from the server
    static void HandlePacket(const Packets::Mirror::MirrorBlips& packet);
    // the authority: a blip of its mission that each player gets on his own (the objective vehicle that every player of
    // the mission gets a copy of, CMissionSyncRules) stops being mirrored; the receivers that have it remove it
    static void Disown(uint32_t handle);
    // a receiver: the host's mission ended, so every blip mirrored from it goes (`mirror-blips-clear-on-end`; the ones
    // the host marked to remain stay)
    static void OnHostMissionEnded();
    // the authority: a player joined or went back to coop, send him the whole owned set
    static void OnPlayerJoined(playerid_t playerId);

    // the static path (M-D1): the host's CNetworkStaticBlip::Send skips the traces of owned blips, a receiver's
    // CNetworkStaticBlip::Create leaves the traces this module made
    static bool IsOwnedTrace(int traceIndex);
    static bool IsMirrorTrace(int traceIndex);

    // --coopdiag: one summary line (reads only plain cached values)
    static void WriteDiagSummary(FILE* file);
};
