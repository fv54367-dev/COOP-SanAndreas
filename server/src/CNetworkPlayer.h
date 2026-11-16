#pragma once

#include "enet/enet.h"

#include "CVector.h"
#include "CNetwork.h"
#include <string>
#include <network/ids.h>
#include <network/packets/players.h>
#include <CPedClothesDesc.h>
class CNetworkPed;
class CNetworkPlayer
{
public:
    CNetworkPlayer(ENetPeer* peer, playerid_t playerid);

    std::string GetName();

    ENetPeer* m_pPeer;
    playerid_t m_playerId;
    char m_Name[32 + 1] = {0};
    bool m_bIsHost = false;
    // E: whether the host has a coop mission running (from ON_MISSION_FLAG_SYNC); a host that leaves with it active
    // aborts the mission for everyone
    bool m_bOnMission = false;
    // connected while the script authority was already on a mission: that mission is not his, its packets don't reach
    // him until it ends (docs/REQUISITOS.md 1)
    bool m_bLateForMission = false;
    // plays the missions alone (F1, PLAYER_MISSION_MODE): no packet of the authority's mission reaches him
    bool m_bIndividualMode = false;
    int m_nRunningMission = -1;  // the mission this player runs (PLAYER_MISSION_MODE), -1: none
    // plays with the original main.scm (PLAYER_MISSION_MODE): the missions of a player with the other scripts aren't his
    bool m_bVanillaScripts = false;
    int8_t m_nSeatId = -1;
    vehicleid_t m_vehicleId{};
    bool m_bCorrectVersion = false;
    float m_afStats[14]{};
    Packets::Players::PlayerPlaceWaypoint m_waypointState{};
    CPedClothesDesc m_pPedClothesDesc{};

    // B1: where the server last saw the player (updated by his on-foot / driver / passenger updates)
    CVector m_vecPos{};
    uint8_t m_nAreaCode = 0;
    uint8_t m_nPopulationArea = 0;
    CVector m_vecPopulationPos{};
    uint32_t m_nPopulationSeen = 0;
    uint64_t m_nPopulationOrder = 0;
    bool m_bSocialAlive = false;
    bool m_bSocialControls = false;
    uint32_t m_nVoiceWindow = 0;
    uint32_t m_nVoiceFrames = 0;
    uint32_t m_nPosTime = 0;  // g_serverTime (ms) of the last position update

    // G2: invalid packets received from this player (bad type, size or deserialization) and the start of the
    // 10 s window they are counted in (see CPacketFactory::Receive)
    uint32_t m_nInvalidPackets = 0;
    uint32_t m_nInvalidPacketsWindowStart = 0;
    // G2: a disconnect was already requested for him: don't send it again while enet finishes it
    bool m_bKickRequested = false;

    struct
    {
        uint8_t bStatsModified : 1;
        uint8_t bClothesModified : 1;
    } m_ucSyncFlags{};
    std::vector<CNetworkPed*> m_vPedClaims;

    void RemoveFromVehicle();

    ~CNetworkPlayer() = default;
};