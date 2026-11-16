#pragma once

#include <cstdint>

class CRunningScript;

// Phase 4 of docs/PLAN_CPP.md: what the mission scripts asked for with Coop.* lines, decided here so the scripts can go
// back to the original ones (analysis: docs/FASE4_ANALISIS.md). Switch `mission-sync-rules`.
//  - Synced scripts: the 100 scripts that call Coop.EnableSyncingThisScript do it on the line after script_name, with no
//    condition, so registering their names up front is the same (the list only grows). Side missions are not here: they
//    are registered when the player picks coop (CCoopMission).
//  - Teleport to the host: 657 of the 661 Coop.TeleportAllNetworkPlayersToHost come right after the mission moves the
//    local player (SET_CHAR_COORDINATES 00A1, WARP_CHAR_FROM_CAR_TO_COORD 0362, WARP_CHAR_INTO_CAR 036A,
//    WARP_CHAR_INTO_CAR_AS_PASSENGER 0430). On the script authority such a move of its own player by a synced mission
//    script teleports the mission's players behind him once the scripts of the frame ran (the heading and interior the
//    script sets right after are taken). The missions that place each player by hand (CMirrorBlips per-player list)
//    keep their own Coop lines.
//  - Wait for everybody at a red cylinder: a LOCATE_CHAR_* (2D 00EC-00F1, 3D 00FE-0103) with the sphere flag on the
//    authority's own player in a synced mission script is only true once every player of the mission is near it too
//    (what SWEET4, INTRO2 and TWAR7 do by hand). The raw result is corrected in CRunningScript::UpdateCompareFlag
//    (0x4859D0, gta-reversed RunningScript.cpp), before NOT and AND/OR; a check with NOT ("he left the area") is not
//    touched. The host is told in the chat who he is waiting for.
//  - Story mission start: CAN_PLAYER_START_MISSION (03EE) of a story launcher (SWEET, SMOKE, RYDER...) follows the coop
//    start rules of Coop.LocateAllPlayersOnFoot3D: in coop only the authority starts it, with every player of the
//    mission next to him; alone it starts as in the game; the story lock and Sweet's progress check apply.
class CMissionSyncRules
{
public:
    static void InjectHooks();
    // a story chain's counter as this player's own progress: the value a guest launcher copied from the host reads as
    // what he had before (the others compare progress with it; the save keeps it too)
    static int OwnChainCounter(int chain, int current);
    // /igualar: the counters now in memory are the player's own progress (the copied value no longer reads as the old one)
    static void ForgetOwnCounters();
    static bool IsEnabled();
    // the rules only apply to the story missions (the table below); side missions keep their own behaviour: the
    // paramedic's hospital or a courier race must not wait for players doing their own part elsewhere
    static bool IsStoryScript(CRunningScript* pScript);
    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame
    static void Process();
    // Flush after the normal entry/exit snapshot in FinishedProcessingScripts.
    static void SendHoodDoorCorrection();
    // Coop.TeleportAllNetworkPlayersToHost does nothing: the rule above does it
    static bool MutesTeleportCommand(CRunningScript* pScript);
    // Coop.Update/RemoveCheckpointForAllNetworkPlayers do nothing: 534 of the 545 updates come with Blip.AddForCoord at
    // the same place (mirrored by CMirrorBlips) and the cylinder is the host's own LOCATE one (CMissionAreaMarkers)
    static bool MutesCheckpointCommands(CRunningScript* pScript);
    // T98: MISSION_SCRIPTED_EXIT with this opcode (none of the game's) only shows the guest the help box "Solo <host>
    // puede manejar este vehiculo en esta mision" (mission-host-driver-hint); it never takes him out
    static constexpr uint16_t HOST_DRIVER_HINT_OPCODE = 0x7FFF;
};
