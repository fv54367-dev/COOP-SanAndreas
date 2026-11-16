#pragma once

// One player per mission id (docs/REQUISITOS.md 1): the player that starts a mission claims its id; nobody else can start
// that same mission until the owner finishes, fails or leaves. Other missions are not affected. With the switch off
// (`mission-lock`) every claim is granted and the host's individual launches go to everybody, as before.
class CMissionLock
{
public:
    static constexpr int MAX_MISSIONS = 201;  // ids 0..200 (same range as LaunchMissionLocal)

    static void OnRequest(CNetworkPlayer* pNetworkPlayer, int missionId);
    static void OnRelease(CNetworkPlayer* pNetworkPlayer, int missionId);
    static void OnPlayerLeft(CNetworkPlayer* pNetworkPlayer);
    static void SendSnapshot(CNetworkPlayer* pNetworkPlayer);

private:
    static void Broadcast(int missionId, bool locked, playerid_t ownerId);

    inline static playerid_t ms_owner[MAX_MISSIONS]{};
};
