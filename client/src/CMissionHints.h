#pragma once
#include <string>
#include <cstdint>
#include "network/ids.h"

class CRunningScript;

// Hints in the game's own style (native font): the help box (top left) and the mission text (bottom)
class CMissionHints
{
public:
    static bool Enabled();

    // text that is not in the GXT in the help box (CHud::SetHelpMessage copies it); '~' and non-ASCII are replaced
    static void Help(const std::string& text);

    // the game's fonts have no accents: a -> a, n -> n (UTF-8 or Latin-1), '~' -> '-', other non-ASCII dropped
    static std::string ToGameText(const std::string& text);

    // a story launcher asks CAN_PLAYER_START_MISSION while this player stands in its marker and the coop rules say no:
    // when the mission it would start next only starts at some hours, shows the game's own "come back at..." text
    // (MTIMEx, what the launcher prints after the check this player never reaches)
    static void OnLauncherAsked(CRunningScript* pLauncher);

    // mission-wait-notices: the script authority waits for players of the mission (kind = MissionWaitNotice::Kind;
    // missing = their names, ", " between them). Shows it in his help box and sends it to the others, once a second.
    static bool WaitNoticesEnabled();
    static void AuthorityWait(uint8_t kind, const std::string& missing);
    // a guest standing in a start marker the authority is not at (nobody sends a notice then): his own help box
    static void LocalStartWait(const std::string& missing);
    // the authority is the one missing in a marker of the mission a guest already stands in: "they wait for you" in his
    // help box, and "waiting for <him>" to the others
    static void AuthorityAwaited(uint8_t kind);
    static void ReceiveWaitNotice(playerid_t sender, uint8_t kind, const char* missing);
    // after the scripts: takes the notice away once nobody renews it
    static void Process();

private:
    // the help box without the sound every frame: kept up (permanent) while the wait lasts, never over another help
    static void ShowWait(const std::string& text);
    // the notice to the others of the mission, once a second (the server takes it from the authority, or START from anybody)
    static void SendWait(uint8_t kind, const std::string& missing);
};
