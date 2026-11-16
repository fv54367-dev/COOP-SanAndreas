#include "CCutscenePartners.h"
#include "CSocialActions.h"
#include "CMissionMachines.h"
#include "CMissionObjectMoves.h"
#include "CMissionWarTable.h"
#include "CPacketFactory.h"
#include "enet/enet.h"
#include "stdafx.h"
#include "CMissionLock.h"
#include "../shared/semver.h"
#include <cassert>
#include <windows.h>

void CNetwork::Start()
{
    m_networkThread.ClearQueues();
    m_networkThread.Start(InitAsync);
}

DWORD WINAPI CNetwork::InitAsync(LPVOID)
{
    assert(strcmp(m_IpAddress, "") != 0 && "Wrong IP passed");
    // The endpoint code may supply its own port.

    if (enet_initialize() != 0)
    {
        CChat::AddMessage("{63c7e8}[Network] {f07878}No se pudo inicializar la red (enet_initialize).");
        return 0;
    }
    else
    {
        std::cout << "Success to enet_initialize" << std::endl;
    }

    // auto-reconnect: after a session is lost (timeout / server drop) retry every 5 s for 2 minutes
    constexpr int nReconnectDelayMs = 5000;
    constexpr int nMaxReconnectAttempts = 24;  // 24 * 5 s = 2 minutes

    int nReconnectAttempt = 0;

    while (!m_networkThread.IsStopRequested())
    {
        if (nReconnectAttempt > 0)
        {
            m_bAutoReconnecting = true;
            CChat::AddMessage(
                "{63c7e8}[Network] {e8b86d}Conexion perdida, reintentando (%d)...", nReconnectAttempt);

            // wait between attempts, but keep answering a stop request (game closing, intentional disconnect)
            for (int nWaited = 0; nWaited < nReconnectDelayMs && !m_networkThread.IsStopRequested(); nWaited += 100)
            {
                Sleep(100);
            }
            if (m_networkThread.IsStopRequested())
                break;
        }

        m_pENetHost = enet_host_create(NULL, 1, (int)ePacketChannel::COUNT, 0, 0);
        if (m_pENetHost == nullptr)
        {
            enet_deinitialize();
            m_bAutoReconnecting = false;
            return 0;
        }

        ENetAddress address;

        address.port = m_nPort;
        if (!ParseAddress(m_IpAddress, address) || address.port == 0) {
            enet_host_destroy(m_pENetHost); m_pENetHost = nullptr;
            return 0;
        }

        uint32_t packedVersion = semver_parse(COOPSA_VERSION, nullptr);  // v0.1.0-alpha compat
        m_pPeer = enet_host_connect(m_pENetHost, &address, (int)ePacketChannel::COUNT, packedVersion);
        if (m_pPeer == nullptr)
        {
            CChat::AddMessage("{63c7e8}[Network] {ff0000}m_pPeer == NULL.");
            enet_host_destroy(m_pENetHost);
            m_pENetHost = nullptr;
            enet_deinitialize();
            m_bAutoReconnecting = false;
            return 0;
        }

        CChat::AddMessage("{63c7e8}[Network] {d6dce8}Conectando con el servidor...");
        ENetEvent event;
        uint32_t lastRetryingMsg = 0;
        uint32_t connectStartedAt = GetTickCount();
        while (!m_bConnected && !m_networkThread.IsStopRequested())
        {
            if (enet_host_service(m_pENetHost, &event, 100) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
            {
                ++m_nSessionId;  // before m_bConnected: the game thread must never pair the new session with the old id
                m_bConnected = true;
                m_bAuthenticated = false;
                m_bHandshakePending = true;
                m_bAutoReconnecting = false;

                CChat::AddMessage("{63c7e8}[Network] {78d99a}Conectado {cecedb}al servidor.");
            }
            else if (!m_networkThread.IsStopRequested())
            {
                if (GetTickCount() - lastRetryingMsg >= 2000)
                {
                    CChat::AddMessage("{63c7e8}[Network] {e8b86d}No se pudo conectar. Reintentando...");
                    lastRetryingMsg = GetTickCount();
                }

                // the first connection keeps retrying forever (as before); a reconnect attempt gives up after 5 s so
                // the 2 minute budget is honoured
                if (nReconnectAttempt > 0 && GetTickCount() - connectStartedAt >= (uint32_t)nReconnectDelayMs)
                {
                    break;
                }
            }
        }

        if (m_networkThread.IsStopRequested())
            break;

        if (!m_bConnected)
        {
            // a reconnect attempt that could not even reach the server: drop this host / peer and count it
            if (m_pPeer != nullptr)
            {
                enet_peer_disconnect_now(m_pPeer, 0);
                m_pPeer = nullptr;
            }
            if (m_pENetHost != nullptr)
            {
                enet_host_destroy(m_pENetHost);
                m_pENetHost = nullptr;
            }
            m_bConnected = false;
            m_bAuthenticated = false;

            if (!CConfigLoader::ms_bAutoReconnect || ++nReconnectAttempt > nMaxReconnectAttempts)
            {
                m_bAutoReconnecting = false;
                break;
            }

            m_bAutoReconnecting = true;
            continue;
        }

        // the game thread authenticates us during the session; it is needed to tell an intended kick from a drop
        bool bSessionAuthenticated = false;
        uint32_t nDisconnectReason = 0;
        bool bDisconnected = false;

        while (!m_networkThread.IsStopRequested() && !bDisconnected)
        {
            std::deque<CNetworkThread::QueuedPacket> packetsToSend = m_networkThread.TakeOutgoing();

            for (const CNetworkThread::QueuedPacket& queuedPacket : packetsToSend)
            {
                uint32_t flags = queuedPacket.reliability == ePacketReliability::RELIABLE ? ENET_PACKET_FLAG_RELIABLE : 0;
                ENetPacket* packet = enet_packet_create(queuedPacket.data.data(), queuedPacket.data.size(), flags);
                if (packet != nullptr)
                {
                    enet_peer_send(m_pPeer, static_cast<uint8_t>(queuedPacket.channel), packet);
                    ms_nBytesSentThisSecondCounter += static_cast<uint32_t>(queuedPacket.data.size());
                    ++m_nSentPackets;
                }
            }

            while (enet_host_service(m_pENetHost, &event, 1) > 0)
            {
                switch (event.type)
                {
                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        ms_nBytesReceivedThisSecondCounter += event.packet->dataLength;
                        ++m_nReceivedPackets;
                        std::vector<uint8_t> data(event.packet->data, event.packet->data + event.packet->dataLength);
                        m_networkThread.EnqueueIncoming(std::move(data));
                        enet_packet_destroy(event.packet);
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT:
                        bSessionAuthenticated = m_bAuthenticated;
                        nDisconnectReason = event.data;  // 0 on timeout / plain drop, the motive when the server kicks us
                        m_bConnected = false;
                        m_bAuthenticated = false;
                        bDisconnected = true;
                        break;
                    default:
                        break;
                }

                if (bDisconnected)
                    break;
            }

            if (m_pPeer != nullptr)
            {
                m_nRTT = m_pPeer->roundTripTime;
                m_nRTTVariance = m_pPeer->roundTripTimeVariance;
                m_nDroppedPackets = m_pPeer->packetsLost;
                m_nPacketLoss = m_pPeer->packetLoss;
            }
        }

        // this session is over: get rid of its host / peer before deciding what to do next
        if (m_pPeer != nullptr)
        {
            enet_peer_disconnect_now(m_pPeer, 0);
            m_pPeer = nullptr;
        }
        if (m_pENetHost != nullptr)
        {
            enet_host_destroy(m_pENetHost);
            m_pENetHost = nullptr;
        }
        m_bConnected = false;
        m_bAuthenticated = false;

        if (m_networkThread.IsStopRequested())
            break;  // game closing or the player disconnected on purpose (rejection, kick packet)

        if (!bDisconnected || !CConfigLoader::ms_bAutoReconnect || !bSessionAuthenticated || nDisconnectReason != 0 ||
            nReconnectAttempt >= nMaxReconnectAttempts)
        {
            m_bAutoReconnecting = false;
            break;  // today's behaviour: the thread ends and the client stays disconnected
        }

        // the dead session entities are dropped by the game thread, and the handshake re-registers us
        m_bAutoReconnecting = true;
        m_bSessionResetPending = true;
        nReconnectAttempt++;
    }

    enet_deinitialize();
    m_bConnected = false;
    m_bAuthenticated = false;
    m_bAutoReconnecting = false;
    m_bHandshakePending = false;
    return true;
}

void CNetwork::ProcessReceive()
{
    if (m_bHandshakePending.exchange(false))
    {
        Packets::System::PlayerConnected connectedPacket{};
        connectedPacket.payload.isAlreadyConnected = false;
        connectedPacket.payload.playerid = -1;
        connectedPacket.payload.version = semver_parse(COOPSA_VERSION, nullptr);
        strcpy_s(connectedPacket.payload.name, CLocalPlayer::m_Name);
        GetPacketFactory().Send(connectedPacket);
    }

    std::deque<std::vector<uint8_t>> packets = m_networkThread.TakeIncoming();

    for (const std::vector<uint8_t>& packet : packets)
        GetPacketFactory().Receive(packet.data(), static_cast<int>(packet.size()));
}

void CNetwork::SendPacket(
    const uint8_t* data, int dataSize, ePacketChannel packetChannel, ePacketReliability packetReliability)
{
    if (!CNetwork::m_bConnected)
    {
#if DEBUG
        if (data != nullptr)
        {
            std::cerr << "PacketType #" << std::to_string(*reinterpret_cast<const uint16_t*>(data))
                      << " was sent while not connected";
        }
#endif
        return;
    }

    if (data == nullptr || dataSize <= 0)
        return;

    CNetworkThread::QueuedPacket queuedPacket;
    queuedPacket.data.assign(data, data + dataSize);
    queuedPacket.channel = packetChannel;
    queuedPacket.reliability = packetReliability;
    m_networkThread.EnqueueOutgoing(std::move(queuedPacket));
}

void CNetwork::Disconnect()
{
    m_bConnected = false;
    m_bAuthenticated = false;
    m_networkThread.Stop();
}

// auto-reconnect: drops the entities mirrored from the session that just died so the new handshake rebuilds them
// with fresh ids; the entities we host ourselves are left alone (they keep belonging to this client)
void CNetwork::ResetSessionState()
{
    CSocialActions::Reset();
    CMissionMachines::Reset();
    CMissionObjectMoves::Reset();
    CMissionWarTable::Reset();
    CCutscenePartners::Reset();
    CMissionLock::Reset();
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        delete pNetworkPlayer;  // the destructor releases / deletes the mirrored ped
    }
    CNetworkPlayerManager::m_pPlayers.clear();

    // the remote vehicles go before the remote peds: a vehicle warps out the occupants it may still have
    for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
    {
        CNetworkVehicle* pNetworkVehicle = *it;
        if (!pNetworkVehicle->m_bSyncing)
        {
            delete pNetworkVehicle;  // the destructor deletes the game vehicle it mirrored
            it = CNetworkVehicleManager::m_pVehicles.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        CNetworkPed* pNetworkPed = *it;
        if (!pNetworkPed->m_bSyncing)
        {
            delete pNetworkPed;  // the destructor deletes the game ped it mirrored
            it = CNetworkPedManager::m_pPeds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
