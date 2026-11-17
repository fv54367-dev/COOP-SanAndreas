#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include "network/ids.h"

class CNetworkPlayer;

namespace Packets::Scripts
{
class StoryHelp;
class StoryContacts;
}

// story-help-optional: a story mission only waits for the players that have it pending (their own chain counter is the
// authority's). A guest that already did it, or has not reached it, is asked "help <authority> in <mission>? Y/N":
//  - yes: he is part of the mission as a helper (no progress saved for him: CStoryProgress::Apply skips him)
//  - no / no answer: he drops to individual mode for that mission (the F1 path, ms_bIndividual) and comes back when the
//    authority's mission ends (END), or when the authority leaves
// Also the contact points (map icons) a passed story mission removed and made travel to the players that had it pending,
// who apply them to their own contacts (contacts-follow-host off). Everything runs on the game thread.
class CStoryHelp
{
public:
    static constexpr uint32_t OFFER_TIMEOUT_MS = 12000;      // the guest's Y / N window
    static constexpr uint32_t OFFER_WAIT_MS = 14500;         // the authority gives up (latency margin)
    static constexpr uint32_t CONTACT_SETTLE_MS = 4000;      // the launcher makes the next contact after the mission ends

    enum eRole : uint8_t
    {
        ROLE_PENDING = 0,  // same point of the chain: waited for, as always
        ROLE_UNOFFERED,    // not pending, no offer sent yet
        ROLE_ASKING,       // offer sent, no answer
        ROLE_HELPER,       // accepted: waited for like a pending one
        ROLE_DECLINED,     // said no (or did not answer): out of this mission
    };

    static bool Enabled();

    // authority: how the player takes part in the story mission a launcher of `chain` starts
    static eRole GetRole(const CNetworkPlayer* pNetworkPlayer, int chain);
    // authority: called every frame while he stands in the start marker with the players of the mission next to him
    static void GateTouch();
    // authority: sends the offers still to send; true while somebody has not answered (names in `asking`)
    static bool AskNotPending(int chain, std::string& asking);
    // authority: his story mission started / ended (CStoryProgress)
    static void OnMissionLaunched();
    static void OnMissionEnded(bool bPassed);

    // guest: the authority starts a mission of `chain` for which his own progress differs: shows "wait for the invitation"
    // (every 5 s) and returns true. False when he is pending (or the authority is unknown).
    static bool GuestNotPending(int chain);

    // guest: the local player is a helper or declined the running story mission: no progress, no contacts for him
    static bool IsNotPending() { return ms_bNotPending; }

    static void OnPacket(const Packets::Scripts::StoryHelp& packet);
    static void OnContacts(const Packets::Scripts::StoryContacts& packet);

    static void Process();
    static void Draw();

private:
    struct SOffer
    {
        playerid_t id{};
        uint8_t eState = ROLE_ASKING;
        uint32_t nTick = 0;
    };

    static void SendTo(playerid_t target, uint8_t kind, const char* szMission);
    static void CancelOffers();
    static void ReplyToOffer(bool bAccept);
    static void LeaveHelp();
    static void SendContactDelta();

    // authority
    static inline std::vector<SOffer> ms_vOffers;
    static inline uint32_t ms_nGateTick = 0;
    static inline bool ms_bMissionLaunched = false;
    static inline bool ms_bContactsBefore = false;
    static inline bool ms_bContactsPending = false;
    static inline uint32_t ms_nContactsDeadline = 0;

    // guest
    static inline bool ms_bOfferOpen = false;
    static inline bool ms_bNotPending = false;
    // END comes before the contact delta (sent CONTACT_SETTLE_MS later): one that was not pending ignores it until then
    static inline uint32_t ms_nIgnoreContactsUntil = 0;
    static inline bool ms_bAutoIndividual = false;  // this class made him individual (and gives it back)
    static inline playerid_t ms_authorityId{};
    static inline char ms_szMission[40]{};
    static inline uint32_t ms_nOfferDeadline = 0;
    static inline uint32_t ms_nOfferTick = 0;
};
