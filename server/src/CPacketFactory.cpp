#include "network/packet.h"
#include "network/packet_handler.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "serialize.h"
#include "stdafx.h"
#include "CPacketFactory.h"

static uint8_t packetBuffer[10 * 1024];

// G2: a packet is rejected as invalid past this many in this window
static constexpr uint32_t MAX_INVALID_PACKETS = 50;
static constexpr uint32_t INVALID_PACKETS_WINDOW_MS = 10000;

// G2: a packet from a player couldn't be read (type, size or a failed deserialization): log it, count it and, past
// the limit inside the window, disconnect the player with the reason (CPacketFactory::Receive)
static void RejectPacket(CNetworkPlayer* pNetworkPlayer, const char* reason, uint32_t packetType)
{
    logger::warn("%s sent an invalid packet (#%u): %s", pNetworkPlayer->GetName().c_str(), packetType, reason);

    if (!CConfigManager::GetConfigPacketValidation() || pNetworkPlayer->m_bKickRequested)
        return;

    if (pNetworkPlayer->m_nInvalidPackets == 0 ||
        g_serverTime - pNetworkPlayer->m_nInvalidPacketsWindowStart > INVALID_PACKETS_WINDOW_MS)
    {
        pNetworkPlayer->m_nInvalidPacketsWindowStart = g_serverTime;
        pNetworkPlayer->m_nInvalidPackets = 0;
    }

    if (++pNetworkPlayer->m_nInvalidPackets > MAX_INVALID_PACKETS)
    {
        pNetworkPlayer->m_bKickRequested = true;
        logger::warn("%s sent more than %u invalid packets in %u s: disconnecting him",
            pNetworkPlayer->GetName().c_str(), MAX_INVALID_PACKETS, INVALID_PACKETS_WINDOW_MS / 1000);
        CNetwork::KickPlayer(
            pNetworkPlayer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_INVALID_PACKETS);
    }
}

static bool BuildPacketStream(Packet& packet, serialize::WriteStream& writeStream, ePacketChannel& outPacketChannel,
    ePacketReliability& outPacketReliability)
{
    memset(packetBuffer, 0, sizeof(packetBuffer));  // is this necessary?

    writeStream = serialize::WriteStream(packetBuffer, sizeof(packetBuffer));

    uint16_t packetType = static_cast<uint16_t>(packet.GetType());
    writeStream.SerializeBits(packetType, 16);

    if (packet.GetChannel() != ePacketChannel::SYSTEM)
    {
        // a missing or far-future client timestamp would make receivers buffer the packet; use the server clock
        if (packet.serverTime == 0 || packet.serverTime > g_serverTime + 1000)
        {
            packet.serverTime = g_serverTime;
        }
        writeStream.SerializeBits(packet.serverTime, 32);
    }

    if (!packet.SerializeWrite(writeStream))
    {
        logger::warn("SerializeWrite (packet type #%d) returned false", packetType);
        return false;
    }
    writeStream.Flush();

    outPacketChannel = packet.GetChannel();
    outPacketReliability = GetChannelReliability(outPacketChannel);
    return true;
}

void CPacketFactory::Send(Packet& packet, CNetworkPlayer* pNetworkPlayer)
{
    serialize::WriteStream writeStream;
    ePacketChannel packetChannel{};
    ePacketReliability packetReliability{};
    if (!BuildPacketStream(packet, writeStream, packetChannel, packetReliability))
        return;

    CNetwork::SendPacket(
        pNetworkPlayer, writeStream.GetData(), writeStream.GetBytesProcessed(), packetChannel, packetReliability);
}

void CPacketFactory::SendPacketNoAuth_ENet(Packet& packet, ENetPeer* pENetPeer)
{
    serialize::WriteStream writeStream;
    ePacketChannel packetChannel{};
    ePacketReliability packetReliability{};
    if (!BuildPacketStream(packet, writeStream, packetChannel, packetReliability))
        return;

    CNetwork::SendPacketNoAuth_ENet(
        pENetPeer, writeStream.GetData(), writeStream.GetBytesProcessed(), packetChannel, packetReliability);
}

void CPacketFactory::SendToAll(Packet& packet, CNetworkPlayer* pNetworkPlayerToIgnore)
{
    serialize::WriteStream writeStream;
    ePacketChannel packetChannel{};
    ePacketReliability packetReliability{};
    if (!BuildPacketStream(packet, writeStream, packetChannel, packetReliability))
        return;

    CNetwork::SendPacketToAll(writeStream.GetData(), writeStream.GetBytesProcessed(), packetChannel, packetReliability,
        pNetworkPlayerToIgnore);
}

void CPacketFactory::ReceivePacketNoAuth_ENet(const uint8_t* data, int dataSize, ENetPeer* pENetPeer)
{
    serialize::ReadStream readStream = serialize::ReadStream(data, dataSize);

    uint32_t tempPacketType = 0;
    if (!readStream.SerializeBits(tempPacketType, 16))
    {
        logger::warn("SerializeBits couldn't read tempPacketType");
        return;
    }

    ePacketType packetType = static_cast<ePacketType>(tempPacketType);
    if (packetType != ePacketType::PLAYER_CONNECTED)
    {
        logger::warn("An unauthorized player is trying to send a packet type #%d %i.%i.%i.%i:%u", tempPacketType,
            pENetPeer->address.host & 0xFF, (pENetPeer->address.host >> 8) & 0xFF,
            (pENetPeer->address.host >> 16) & 0xFF, (pENetPeer->address.host >> 24) & 0xFF, pENetPeer->address.port);
        return;
    }

    Packets::System::PlayerConnected playerConnected{};
    if (!playerConnected.SerializeRead(readStream))
    {
        logger::warn("Packets::System::PlayerConnected::SerializeRead returned false");
        return;
    }
    CNetwork::HandlePlayerConnected(pENetPeer, playerConnected);
}

void CPacketFactory::Receive(const uint8_t* data, int dataSize, CNetworkPlayer* pNetworkPlayer)
{
    serialize::ReadStream readStream = serialize::ReadStream(data, dataSize);

    uint32_t tempPacketType = 0;
    if (!readStream.SerializeBits(tempPacketType, 16))
    {
        RejectPacket(pNetworkPlayer, "the packet type couldn't be read", tempPacketType);
        return;
    }
    ePacketType packetType = static_cast<ePacketType>(tempPacketType);

    // G2: a type outside the enum is rejected before the lookup (m_map[packetType] would insert it)
    if (tempPacketType >= static_cast<uint32_t>(ePacketType::PACKET_ID_MAX))
    {
        RejectPacket(pNetworkPlayer, "packet type out of range", tempPacketType);
        return;
    }

    auto it = m_map.find(packetType);
    if (it == m_map.end() || it->second == nullptr)
    {
        RejectPacket(pNetworkPlayer, "unknown packet type", tempPacketType);
        return;
    }
    Packet* pPacket = it->second;

    if (pPacket->GetChannel() != ePacketChannel::SYSTEM)
    {
        if (!readStream.SerializeBits(pPacket->serverTime, 32))
        {
            RejectPacket(pNetworkPlayer, "the server time couldn't be read", tempPacketType);
            return;
        }
    }

    if (!pPacket->SerializeRead(readStream))
    {
        RejectPacket(pNetworkPlayer, "deserialization failed", tempPacketType);
        return;
    }

    // G2: a whole spare byte (or more) means the packet had more data than it reads: the sender built it wrong or is
    // trying to smuggle fields. The writer pads to the byte boundary, so a valid packet never leaves 8+ spare bits
    if ((int64_t)dataSize * 8 - readStream.GetBitsProcessed() >= 8)
    {
        RejectPacket(pNetworkPlayer, "trailing data", tempPacketType);
        return;
    }

    GetPacketHandler().ProcessPacket(pPacket, pNetworkPlayer);
}

void CPacketFactory::RegisterPacket(Packet* pPacket)
{
    COOP_VERIFY(pPacket != nullptr);

    ePacketType packetType = pPacket->GetType();

    COOP_VERIFY(m_map.find(packetType) == m_map.end());

    m_map[packetType] = pPacket;
}