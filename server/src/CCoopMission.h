#pragma once

#include <cstdint>
#include <vector>

#include "network/ids.h"

class CNetworkPlayer;
class Packet;

namespace Packets::Scripts
{
class CoopMissionState;
}

// F: cooperative side mission with an owner. The player that starts a side mission is its "owner" (his game runs the
// script) and the server keeps who owns it and who joined. There is only one at a time; while it runs the owner, not
// the host, is the script authority, and only the participants get the mission packets. With the switch off
// (`coop-side-missions`) nothing here is active and every check falls back to the host, as before.
class CCoopMission
{
public:
    static constexpr uint32_t OFFER_TIMEOUT_MS = 12000;
    static constexpr int MAX_PARTICIPANTS = 4;

    enum ePhase : uint8_t
    {
        PHASE_FREE = 0,
        PHASE_OFFER = 1,  // the slot is taken, the invited players are deciding (12 s at most)
        PHASE_RUNNING = 2,
    };

    enum eReason : uint8_t
    {
        REASON_OK = 0,
        REASON_BUSY = 1,             // another coop mission is already active
        REASON_NOBODY_ACCEPTED = 2,  // the offer ended with just the owner: the mission stays individual
        REASON_OWNER_LEFT = 3,       // the owner gave up
        REASON_DISCONNECT = 4,       // the owner disconnected
        REASON_FINISHED = 5,         // the owner's mission ended (passed or failed)
        REASON_PARTICIPANT_FELL = 6, // a guest died or was arrested: the owner fails the mission for everyone
    };

    // network loop: the offer times out (start with whoever accepted, or release the slot)
    static void Process();

    // the slot is taken (offer in progress or mission running): only one coop mission at a time
    static bool IsActive();
    // the mission is running: its owner is the script authority and its participants get the mission packets
    static bool IsRunning();

    // console `status`: what the active coop mission is, if any
    static uint8_t GetPhase();
    static playerid_t GetOwnerId();
    static const char* GetTitle();

    // running -> the owner; otherwise -> the host (story missions stay as today)
    static bool IsScriptAuthority(CNetworkPlayer* pNetworkPlayer);
    static bool IsOwner(CNetworkPlayer* pNetworkPlayer);
    static bool IsParticipant(CNetworkPlayer* pNetworkPlayer);
    static bool IsParticipant(playerid_t playerid);
    // while a coop mission runs, only its participants receive the mission packets
    static bool CanReceiveMissionPacket(CNetworkPlayer* pNetworkPlayer);
    // plays the missions alone (F1 individual) or with the other scripts than the host: the host's mission isn't his
    static bool IsOutOfHostMission(CNetworkPlayer* pNetworkPlayer);
    // the script authority is on a mission right now (a player connecting now is late for it)
    static bool IsAuthorityOnMission();
    // the authority's mission is over: the late players get the next one
    static void ClearLatePlayers();

    // a mission packet from the authority: to the participants while a coop mission runs, to everyone otherwise
    // bWorldState: objects and vehicle flags, which also reach the players in individual mode
    static void Relay(Packet& packet, CNetworkPlayer* pSender, CNetworkPlayer* pNetworkPlayerToIgnore = nullptr,
        bool bWorldState = false);
    static void SendToParticipants(Packet& packet, CNetworkPlayer* pNetworkPlayerToIgnore = nullptr);

    // packet handlers (COOP_MISSION_OFFER / REPLY / LEAVE / END)
    static void OnOffer(CNetworkPlayer* pNetworkPlayer, const char* szScriptName, const char* szTitle);
    static void OnReply(CNetworkPlayer* pNetworkPlayer, bool bAccept);
    static void OnLeave(CNetworkPlayer* pNetworkPlayer, bool bFell = false);
    static void OnEnd(CNetworkPlayer* pNetworkPlayer, bool bPassed, bool bCancelled = false);

    // a player that just connected learns which coop mission is active, if any
    static void SendCurrentStateTo(CNetworkPlayer* pNetworkPlayer);

    // the player disconnected: the owner closes the mission for everyone (COOP_MISSION_STATE, reason 4), a
    // participant is just dropped from the list
    static void OnPlayerLeft(CNetworkPlayer* pNetworkPlayer);

private:
    static void Reset();
    static void FinishOffer();
    static void AddParticipant(playerid_t playerid);
    static void RemoveParticipant(playerid_t playerid);
    static void SendState(eReason reason);
    static void SendStateTo(CNetworkPlayer* pNetworkPlayer, eReason reason);
    static void FillState(Packets::Scripts::CoopMissionState& state, eReason reason);

    static inline ePhase ms_ePhase = PHASE_FREE;
    static inline playerid_t ms_ownerId{};
    static inline char ms_szScriptName[8]{};
    static inline char ms_szTitle[24]{};
    static inline playerid_t ms_participants[MAX_PARTICIPANTS]{};
    static inline int ms_nParticipantCount = 0;
    static inline std::vector<playerid_t> ms_vPendingReplies;
    static inline uint32_t ms_nOfferDeadline = 0;
};
