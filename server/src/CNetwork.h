#pragma once

#include "network/packet.h"
#include "network/packets/system.h"
#include <unordered_map>
class CNetworkPlayer;
class CNetwork
{
public:
    CNetwork() {}

    static bool Init(unsigned short port);
    static void SendPacketNoAuth_ENet(ENetPeer* pENetPeer, const uint8_t* data, int dataSize,
        ePacketChannel packetChannel, ePacketReliability packetReliability);
    static void SendPacket(CNetworkPlayer* pNetworkPlayer, const uint8_t* data, int dataSize,
        ePacketChannel packetChannel, ePacketReliability packetReliability);
    static void SendPacketToAll(const uint8_t* data, int dataSize, ePacketChannel packetChannel,
        ePacketReliability packetReliability, CNetworkPlayer* pNetworkPlayerToIgnore = nullptr);

    static void HandlePlayerConnected(ENetPeer* peer, Packets::System::PlayerConnected& playerConnected);
    // G2: send the reason to the player and disconnect him (reason is a PlayerDisconnected::DISCONNECTION_REASON_*)
    static void KickPlayer(CNetworkPlayer* pNetworkPlayer, uint32_t reason);
    ~CNetwork() {}

private:
    static void HandlePeerConnected(ENetEvent& event);
    static void HandlePlayerDisconnected(ENetEvent& event);
};