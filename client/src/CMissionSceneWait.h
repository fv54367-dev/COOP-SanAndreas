#pragma once

class CRunningScript;

// Every cutscene and scripted scene of a coop mission waits for the players of the mission (all missions, 2026-11-12):
// the authority's script is held right before START_CUTSCENE (02E7) or SWITCH_WIDESCREEN on (02A3) until every player
// of the mission is in the same place as he is (same interior or outside, within range). Who is missing is told in the
// help box of everybody (CMissionHints::AuthorityWait). Interruptor: mission-wait-scenes.
// The hold starts at the first command that prepares the scene (fade to black, SELECT_INTERIOR, LOAD_CUTSCENE;
// mission-scene-prelude), before anything reaches the guests, and the guests keep their control meanwhile.
// Also, while the authority is in an interior the others have not entered yet, the whole script stops (mission-hold-on-entry).
class CMissionSceneWait
{
public:
    // called before every command of every script (CommandHooks): true = do not run it now, the script yields and tries
    // again on the next frame
    static bool ShouldHold(CRunningScript* script, unsigned short opcode);
    static bool ShouldHoldCore(CRunningScript* script, unsigned short opcode);
    // called before every command of every script: keeps the tags sprayed while the script was held from being
    // un-sprayed by its own SET_TAG_STATUS_IN_AREA (mission-hold-keep-tags)
    static void OnCommandStart(CRunningScript* script, unsigned short opcode);
    // the last hold of this script was released (the scene started, or it gave up)
    static void Reset();
    // every player of the mission who is not where the authority is stands outside close to the door he went through (they
    // are on their way in: nobody gets teleported yet)
    static bool MissingAreComing();
};
