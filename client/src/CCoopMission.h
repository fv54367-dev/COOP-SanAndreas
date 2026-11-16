#pragma once

#include <cstdint>
#include <vector>

#include "network/ids.h"

class CRunningScript;
class CNetworkPlayer;

namespace Packets::Scripts
{
class CoopMissionInvite;
class CoopMissionState;
}

// F: cooperative side missions (client side). The player that starts a side mission is its "owner": his game runs the
// script and the server keeps who joined. This class mirrors the server state (COOP_MISSION_STATE), decides the mode
// of a side mission (Coop.ChooseSideMissionMode, 0x1D29) and, while a coop mission runs, it is the script authority
// instead of the host. With the switch off (`coop-side-missions`) nothing here is active and every check falls back to
// the host, as before.
class CCoopMission
{
public:
    static constexpr int MAX_PARTICIPANTS = 4;
    static constexpr uint32_t CHOICE_TIMEOUT_MS = 5000;  // Y / N window
    static constexpr uint32_t OFFER_TIMEOUT_MS = 12000;  // matches the server offer timeout
    static constexpr uint32_t OFFER_SAFETY_TIMEOUT_MS = 30000;  // hard cap for a pending offer (F bug 5)
    static constexpr uint32_t ABANDON_HOLD_MS = 1500;    // hold "2" this long to abandon

    enum ePhase : uint8_t
    {
        PHASE_FREE = 0,
        PHASE_OFFER = 1,  // the slot is taken, the invited players are deciding
        PHASE_RUNNING = 2,
    };

    // server state mirror
    static bool IsActive();
    static bool IsRunning();
    static bool IsOwner();
    static bool IsParticipant(playerid_t playerid);
    static bool IsLocalParticipant();
    static playerid_t GetOwnerId();
    static const char* GetTitle();

    // the local player runs the mission script: the coop owner while a coop mission runs, the host otherwise
    static bool IsScriptAuthority();
    // the local player plays alone: he chose individual or the mission he runs is only played alone (Cesar Vialpando). The
    // others are not part of it: no sync of its script, no waiting for them (ShouldIncludeNetworkPlayer is false for all)
    static bool IsLocalIndividualMission();
    // this player dropped out of the running mission (abandoned or fell): its HUD and sync no longer apply to him
    static bool HasLocalLeft() { return ms_bLocalLeft; }
    // the owner's mission script is failed through its death/arrest path (CRunningScript__DoDeatharrestCheck_Hook): the
    // game's FailCurrentMission only reaches scripts flagged as the only mission running, not these
    static void RequestMissionFail() { ms_bForceFail = true; }
    static bool TakeForcedFail()
    {
        const bool bForced = ms_bForceFail;
        ms_bForceFail = false;
        return bForced;
    }
    // while a coop mission runs only its participants are part of the mission (a ped of a player that left is not)
    static bool ShouldIncludeNetworkPlayer(const CNetworkPlayer* pNetworkPlayer);

    // Coop.ChooseSideMissionMode (0x1D29): false while the mode is being decided, true once it is (individual or coop)
    static bool ChooseSideMissionMode(CRunningScript* pScript, const char* szScriptName, const char* szTitle);

    // packets forwarded by the server
    static void OnInvite(const Packets::Scripts::CoopMissionInvite& packet);
    static void OnState(const Packets::Scripts::CoopMissionState& packet);

    static void Process();
    static void Draw();
    static bool ShouldDraw();

    // the state a remote mission left on this client (OnAMissionFlag, checkpoints, blips, camera, pads) is dropped
    static void LocalCleanup();

private:
    enum eChoiceStage : uint8_t
    {
        CHOICE_DECIDING = 0,  // the local player is choosing (Y / N)
        CHOICE_WAITING = 1,   // a coop offer was sent, waiting for the invited players
        CHOICE_INDIVIDUAL = 2,
        CHOICE_COOP = 3,
    };

    struct SChoice
    {
        CRunningScript* pScript = nullptr;
        char szScriptName[8]{};
        char szTitle[24]{};
        uint8_t eStage = CHOICE_DECIDING;
        uint32_t nDeadline = 0;
        uint32_t nSafetyDeadline = 0;  // hard cap while the server mirror holds the slot
    };

    static bool CanOfferCoop();
    static SChoice* FindChoice(CRunningScript* pScript);
    static void PruneChoices();
    static void ResolveWaitingChoices(uint8_t eStage);
    static void ProcessInvite(uint32_t now);
    static void ProcessChoices(uint32_t now);
    static void ProcessAbandon(uint32_t now);
    // N_busted: the local participant fell (death/arrest): he drops out, the mission goes on for the authority
    static void ProcessLocalFall();
    static void ProcessEnd();
    static void ReplyToInvite(bool bAccept);
    static void SendEnd();
    // the local participant drops out of the running mission (holding "2" or falling): the server releases him
    static void LeaveMission(bool bFell = false);

    static inline uint8_t ms_ePhase = PHASE_FREE;
    static inline playerid_t ms_ownerId{};
    static inline char ms_szScriptName[8]{};
    static inline char ms_szTitle[24]{};
    static inline playerid_t ms_participants[MAX_PARTICIPANTS]{};
    static inline int ms_nParticipantCount = 0;

    static inline std::vector<SChoice> ms_vChoices;

    // invite received from the server: we are not the owner and have to answer
    static inline bool ms_bInvitePending = false;
    static inline playerid_t ms_inviteOwnerId{};
    static inline char ms_szInviteScriptName[8]{};
    static inline char ms_szInviteTitle[24]{};
    static inline uint32_t ms_nInviteDeadline = 0;

    // the local player left the mission with "2": he is no longer a participant locally
    static inline bool ms_bLocalLeft = false;
    static inline uint32_t ms_nOwnerRunningSince = 0;  // tick of the last time the owner's script was seen or waited for
    static inline bool ms_bForceFail = false;
    static inline bool ms_bOwnerCancelled = false;  // the owner is giving the mission up (SendEnd tells the others)
    // the owner's mission script was running at least once (used to detect its end)
    static inline bool ms_bOwnerMissionWasRunning = false;
    static inline uint32_t ms_nAbandonHoldStart = 0;
};
