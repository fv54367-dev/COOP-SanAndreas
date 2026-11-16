#pragma once

#include <cstdint>

class CRunningScript;

// Phase 5 of docs/PLAN_CPP.md: the coop / individual choice of a side mission without Coop.ChooseSideMissionMode in its
// script (docs/FASE4_ANALISIS.md section 7). Only with the original scripts (`vanilla-scripts`); with ours the scripts
// still ask for it themselves.
// When a side mission script names itself (SCRIPT_NAME 03A4 after LOAD_AND_LAUNCH_MISSION), it is held asleep (its wake
// time pushed forward every frame) while CCoopMission runs the same offer the Coop command ran; coop: its name is synced;
// individual: it is renamed to the "SOLO" name our scripts used, so a name synced by an earlier coop run doesn't sync it.
// Then it is let go.
class CSideMissionRules
{
public:
    static bool IsEnabled();
    // CMissionMirror::OnCommandExecuted, after every command a script ran
    static void OnCommandExecuted(CRunningScript* pScript, uint16_t opcode);
    // CMissionMirror::OnScriptsProcessed, once per script frame
    static void Process();
};
