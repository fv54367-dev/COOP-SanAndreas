#pragma once

#include <cstdint>
#include <vector>

#include "network/ids.h"

class CRunningScript;

namespace Packets::Scripts
{
class RaceVsLobby;
}

// The lobby of a VS race (docs/MISIONES_MODO.md). The race script (CPRACE) asks Coop.ChooseSideMissionMode when it starts. The
// first player in the marker chooses: Y = VS against the other players, N = alone. On Y the others get an invitation and
// answer; the ones that accept walk into the race marker too and wait there; when everybody who accepted arrived (or after a
// timeout) the owner starts the race and the script of every waiting player goes on at once. Each player runs his own copy of
// the race (no script sync): CVsRace then clears the NPCs, CMinigameVs shows the scores.
class CVsLobby
{
public:
    static void Init();

    // Coop.ChooseSideMissionMode for the race script: false while the lobby holds it, true once the race may go on
    static bool Choose(CRunningScript* pScript);

    static void OnPacket(const Packets::Scripts::RaceVsLobby& packet);

    static bool ShouldDraw();
    static void Draw();
    static void DrawHelpBox();

    // the race goes on with the owner's NPC opponents (mode 2), and whether this game is that owner
    static bool WithNpcs();
    static bool IsOwnerLocal();

private:
    enum eRole : uint8_t
    {
        ROLE_NONE = 0,
        ROLE_DECIDING,       // owner: Y (VS) / N (alone)
        ROLE_WAITING,        // owner: invited the others, waiting for them
        ROLE_STARTED,        // owner: the race goes on (alone or with the ones that arrived)
        ROLE_GUEST_INVITED,  // guest: received the invitation, answering
        ROLE_GUEST_ACCEPTED, // guest: accepted, has to walk into the race marker
        ROLE_GUEST_ARRIVED,  // guest: his race script waits for the owner's start
        ROLE_GUEST_GO,       // guest: the race goes on
        ROLE_OWNER_COUNTDOWN, // owner: sent GO, waits for the common start instant
        ROLE_GUEST_COUNTDOWN, // guest: got GO, waits for the common start instant
        ROLE_ABORT,           // nobody accepted / arrived, or the owner cancelled: the race does not start
    };

    enum eGuestState : uint8_t
    {
        GUEST_INVITED = 0,
        GUEST_ACCEPTED = 1,
        GUEST_DECLINED = 2,
        GUEST_ARRIVED = 3,
    };

    struct SGuest
    {
        playerid_t id{};
        uint8_t state = GUEST_INVITED;
    };

    static void Process();
    static void AbortScript(CRunningScript* pScript);
    static void Send(uint8_t action);
    static void Reset();
    static void StartOwnerRace(bool bWithGuests);
    static bool CanAnswerInvite();
    static bool StartReached();

    static inline uint8_t ms_eRole = ROLE_NONE;
    static inline CRunningScript* ms_pScript = nullptr;
    static inline playerid_t ms_ownerId{};
    static inline uint32_t ms_nDeadline = 0;         // current phase
    static inline uint32_t ms_nStartAt = 0;          // common start instant (server clock)
    static inline uint32_t ms_nArrivalDeadline = 0;  // owner: last moment to wait for the guests to arrive
    static inline std::vector<SGuest> ms_vGuests;
    static inline uint8_t ms_nMode = 0;                  // RaceVsLobby::mode
    static inline CRunningScript* ms_pAborted = nullptr;  // sent to its cleanup: never held again
};
