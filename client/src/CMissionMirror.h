#pragma once

#include <cstdint>
#include <cstdio>

class CRunningScript;
#include <network/ids.h>

// Mission mirror layer, core: texts. What the mission owner's mission shows on screen (objectives, dialogue subtitles, help
// boxes, mission titles) reaches the other players through COpCodeSync's replay, with no lines in the mission scripts.
// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0; docs/design/mirror-layer.md, package M1) and adapted: the
// "host" there is CCoopMission::IsScriptAuthority() here, and money / respect rewards are NOT mirrored (each player keeps
// his own). The rules are in CMirrorPolicy.h. Everything runs on the game thread.
class CMissionMirror
{
public:
    // CCore::Init
    static void Init();

    // --- from COpCodeSync ---
    // after every command a script ran in CRunningScript::Process (BuildAndSendOpcode, before anything else)
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // IsOpcodeSyncable: the mirror wants this command's parameters collected and serialized (inside a script command of
    // the authority: a text of the mission family, or a print_big of any script)
    static bool WantsCommand(CRunningScript* pScript, uint16_t opcode);
    // BuildAndSendOpcode, the serialized command (bMirrorOnly: only the mirror wanted it): true = send it now; false for a
    // print_big that isn't a title
    static bool OnCommandSerialized(CRunningScript* pScript, uint16_t opcode, bool bMirrorOnly, const uint8_t* data,
        int size);
    // the end of CTheScripts::Process (the 0x46A22E redirect): tracks the start and end of the mission
    static void OnScriptsProcessed();
    // COpCodeSync::HandlePacket before it replays a command of the authority: false = don't run it (load_mission_text only
    // names the authority's table here); makes the keys of a replayed print resolve
    static bool OnReplayingCommand(const uint8_t* data, int size);

    // --- events ---
    // the authority: another player joined, send him the mission view (table, objective, help box)
    static void OnPlayerJoined(playerid_t playerId);
    // a receiver: the authority's mission ended (ON_MISSION_FLAG_SYNC 1 -> 0), clear the help box and flash as the clean-up does
    static void OnHostMissionEnded(bool bHostLost);
    // a receiver: the authority changed or the session ended
    static void OnReceiverReset();
    // New Game / load
    static void OnGameRestarted();

    // the mission family (design 3.2) for a script running a command; false when this game is not the authority. For hooks
    // that run inside command handlers: IsFamilyScript(COpCodeSync::GetScriptRunningCommand()).
    static bool IsFamilyScript(CRunningScript* pScript);
    // this game runs a mission whose display is mirrored (a story or side mission, not an intro or an interior script)
    static bool IsHostMissionMirrored();
    // the id of the mission this game runs, -1 when none
    static int GetRunningMissionId();
    // one line in CoopAndreas_mirror.log (diagnostics from other files)
    static void LogEvent(const char* szEvent);
    static void LogEventf(const char* szFormat, ...);

    // receivers (ADD_MESSAGE_GXT and the replayed prints): load the authority's mission table when key needs it
    static void PrepareTextKey(const char* key);
    // a guest about to start a cutscene: its subtitles (CCutsceneMgr::Update -> TheText.Get) need the table its keys are in
    static void EnsureCutsceneTable();

    // --coopdiag: one summary line
    static void WriteDiagSummary(FILE* file);
};
