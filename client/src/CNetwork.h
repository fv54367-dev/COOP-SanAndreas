#pragma once
#include "CNetworkThread.h"
#include "network/packet.h"
#include <cstddef>
#include <atomic>
#include "ConnectionCode.h"

class CNetwork
{
public:
    inline static ENetHost* m_pENetHost = nullptr;
    inline static ENetPeer* m_pPeer = nullptr;
    inline static std::atomic<bool> m_bConnected = false;
    inline static std::atomic<bool> m_bAuthenticated = false;
    // auto-reconnect: the connection dropped and the thread is retrying (used to avoid the "disconnected" line)
    inline static std::atomic<bool> m_bAutoReconnecting = false;
    // auto-reconnect: the game thread must drop the entities of the dead session before the new handshake
    inline static std::atomic<bool> m_bSessionResetPending = false;
    // a new id per established connection; the handshake records the session it authenticated
    inline static std::atomic<uint32_t> m_nSessionId = 0;
    inline static uint32_t m_nAuthenticatedSessionId = 0;
    inline static char m_IpAddress[128 + 1]{};
    inline static unsigned short m_nPort = Config::DEFAULT_PORT;
    inline static std::atomic<uint32_t> ms_nBytesReceivedThisSecond = 0;
    inline static std::atomic<uint32_t> ms_nBytesReceivedThisSecondCounter = 0;
    inline static std::atomic<uint32_t> ms_nBytesSentThisSecond = 0;
    inline static std::atomic<uint32_t> ms_nBytesSentThisSecondCounter = 0;
    inline static CNetworkThread m_networkThread;

    static void Start();
    static DWORD WINAPI InitAsync(LPVOID);
    static void Disconnect();
    // drops the entities of the ended session (the ones the client mirrored from the server) before a reconnect
    static void ResetSessionState();
    static void ProcessReceive();
    static void ProcessSend();
    static void SendPacket(
        const uint8_t* data, int dataSize, ePacketChannel packetChannel, ePacketReliability packetReliability);

    static bool ParseAddress(const char* input, ENetAddress& address)
    {
        if (ConnectionCode::Decode(input, address.host, address.port)) return true;
        std::string host(input);
        auto colon = host.find(':');
        if (colon != std::string::npos) {
            std::string number = host.substr(colon + 1);
            if (number.empty() || number.size() > 5 || number.find_first_not_of("0123456789") != std::string::npos) return false;
            unsigned long port = std::stoul(number);
            if (!port || port > 65535) return false;
            address.port = static_cast<uint16_t>(port);
            host.resize(colon);
        }
        return enet_address_set_host_ip(&address, host.c_str()) >= 0;
    }
    static bool IsValidIP(const char* ip)
    {
        ENetAddress address{};
        address.port = m_nPort;
        return ParseAddress(ip, address) && address.port != 0;
    }

    static bool IsHandshakePending() { return m_bHandshakePending; }
    static uint32_t GetSentCount() { return m_nSentPackets; }
    static uint32_t GetReceivedCount() { return m_nReceivedPackets; }
    static uint32_t GetRTT() { return m_nRTT; }
    static uint32_t GetRTTVariance() { return m_nRTTVariance; }
    static uint32_t GetDroppedCount() { return m_nDroppedPackets; }
    static float GetDroppedRatio() { return m_nPacketLoss * 100.0f / 65536.0f; }

private:
    inline static std::atomic<bool> m_bHandshakePending = false;
    inline static std::atomic<uint32_t> m_nSentPackets = 0;
    inline static std::atomic<uint32_t> m_nReceivedPackets = 0;
    inline static std::atomic<uint32_t> m_nRTT = 0;
    inline static std::atomic<uint32_t> m_nRTTVariance = 0;
    inline static std::atomic<uint32_t> m_nDroppedPackets = 0;
    inline static std::atomic<uint32_t> m_nPacketLoss = 0;
};
