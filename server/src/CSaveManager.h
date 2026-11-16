#pragma once

#include <network/ids.h>

class CNetworkPlayer;

// E: central save of the persistence. "/guardar" (SAVE_ALL_REQUEST), the console `save`, the autosave timer and the
// end of the host's mission all funnel here: every player is asked for his current state (PERSIST_SAVE_REQUEST) and
// the persistence handler writes each answer to his .bin as it arrives.
class CSaveManager
{
public:
    // called every iteration of the network loop
    static void Process();
    // ask everyone to send his state now; pRequester (may be null, e.g. the autosave) gets a chat line with the result
    static void RequestSaveAll(CNetworkPlayer* pRequester);
    // a player answered a PERSIST_SAVE_REQUEST and his state was already written to disk
    static void NotifySaved(CNetworkPlayer* pNetworkPlayer);
    // a player left in the middle of a save-all: he will not answer, drop him from the pending list
    static void OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer);

private:
    static void Finish(bool bTimedOut);
    static void SendChatResult(const char* text);

    static inline std::vector<playerid_t> ms_pendingPlayers;
    static inline playerid_t ms_requesterId{};
    static inline uint32_t ms_nDeadline = 0;
    static inline uint32_t ms_nLastAutoSave = 0;
    static inline bool ms_bSaving = false;
};
