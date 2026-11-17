#include "CNetworkPlayerManager.h"
#include "CRTTBroadcastManager.h"
#include "CPacketFactory.h"
#include "logger.h"
#include "network/packet.h"
#include "network/packets/system.h"
#include "serialize.h"
#include "stdafx.h"

void SendGarageDoorsTo(CNetworkPlayer* pNewPlayer);  // PacketHandlers/world.cpp (T90)
#include "CInternet.h"
#include "build_label.h"
#include <algorithm>
#include "network/packets/voice.h"
#include "network/packets/client_config.h"
#include <network/packets/vehicles.h>
#include <network/packets/peds.h>
#include <network/packets/scripts.h>
#include "CPersistence.h"
#include "CStorySave.h"
#include "CHandover.h"
#include "CModCheck.h"
#include "CCoopMission.h"
#include "CMissionLock.h"
#include "CSaveManager.h"
#include "CServerConsole.h"
#include "CStringUtil.h"
#include <chrono>
#include <list>
#include <random>

// lag simulator (server-config.ini sim-latency-ms, sim-jitter-ms, sim-loss-percent; all 0 = off, sends untouched).
// Outgoing packets wait in a queue until their time; unreliable ones can also be dropped, reliable ones only delayed.
// The order per peer is kept (the jitter bunches packets instead of reordering them).
namespace
{
struct SDelayedPacket
{
    ENetPeer* peer;
    enet_uint32 connectID;
    enet_uint8 channel;
    ENetPacket* packet;
    uint32_t due;
};

std::list<SDelayedPacket> g_delayedPackets;
std::unordered_map<ENetPeer*, uint32_t> g_lastDueByPeer;
uint32_t g_simLatencyMs = 0;
uint32_t g_simJitterMs = 0;
uint32_t g_simLossPercent = 0;
std::mt19937 g_simRandom{std::random_device{}()};

uint32_t SimNowMs()
{
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool IsLagSimEnabled()
{
    return g_simLatencyMs != 0 || g_simJitterMs != 0 || g_simLossPercent != 0;
}

// lag simulator on: queue (or drop) instead of enet_peer_send. The queue holds its own reference on the packet,
// so callers keep freeing it when nobody references it (referenceCount == 0)
void SimSend(ENetPeer* peer, enet_uint8 channel, ENetPacket* packet, bool reliable)
{
    if (!reliable && g_simLossPercent != 0 && g_simRandom() % 100 < g_simLossPercent)
        return;  // lost

    int delay = (int)g_simLatencyMs;
    if (g_simJitterMs != 0)
        delay += (int)(g_simRandom() % (2 * g_simJitterMs + 1)) - (int)g_simJitterMs;
    if (delay < 0)
        delay = 0;

    uint32_t due = SimNowMs() + (uint32_t)delay;
    uint32_t& lastDue = g_lastDueByPeer[peer];
    if ((int32_t)(lastDue - due) > 0)
        due = lastDue;
    lastDue = due;

    packet->referenceCount++;
    g_delayedPackets.push_back({peer, peer->connectID, channel, packet, due});
}

void SimFlush()
{
    if (g_delayedPackets.empty())
        return;

    uint32_t now = SimNowMs();
    for (auto it = g_delayedPackets.begin(); it != g_delayedPackets.end();)
    {
        if ((int32_t)(it->due - now) > 0)
        {
            ++it;
            continue;
        }

        ENetPacket* packet = it->packet;
        packet->referenceCount--;
        // the peer may have left (or the slot was reused by another connection) while the packet waited
        if (it->peer->state == ENET_PEER_STATE_CONNECTED && it->peer->connectID == it->connectID)
            enet_peer_send(it->peer, it->channel, packet);
        if (packet->referenceCount == 0)
            enet_packet_destroy(packet);
        it = g_delayedPackets.erase(it);
    }
}

// launcher query (CoopSA.exe asks before starting the game): the raw datagram "COOPSA?1" on the game port gets
// "COOPSA!1\n<version>\n<build>\n<name>\n<players>\n<max players>\n" back without an ENet connection (ENetHost::intercept)
int ENET_CALLBACK LauncherQuery(ENetHost* host, ENetEvent*)
{
    static const char ask[] = "COOPSA?1";
    if (host->receivedDataLength != sizeof(ask) - 1 || memcmp(host->receivedData, ask, sizeof(ask) - 1) != 0)
        return 0;
    std::string name = CConfigManager::GetConfigName();
    std::replace(name.begin(), name.end(), '\n', ' ');
    const std::string reply = std::string("COOPSA!1\n") + COOPSA_VERSION + "\n" + COOPSA_BUILD + "\n" + name + "\n" +
        std::to_string(CNetworkPlayerManager::m_pPlayers.size()) + "\n" + std::to_string(CConfigManager::GetConfigMaxPlayers()) + "\n";
    ENetBuffer buffer;
    buffer.data = const_cast<char*>(reply.data());
    buffer.dataLength = reply.size();
    enet_socket_send(host->socket, &host->receivedAddress, &buffer, 1);
    return 1;
}
}  // namespace

bool CNetwork::Init(unsigned short port)
{
    if (enet_initialize() != 0)  // try to init enet
    {
        CServerLog::Printf("[ERROR] : ENET_INIT FAILED TO INITIALIZE\n");
        return false;
    }

    ENetAddress address;

    address.host = ENET_HOST_ANY;  // bind server ip
    address.port = port;           // bind server port

    // TODO: `ConfigManager::GetConfigMaxPlayers`
    ENetHost* pENetHost =
        enet_host_create(&address, Config::MAX_SERVER_PLAYERS, (int)ePacketChannel::COUNT, 0, 0);  // create enet host

    if (pENetHost == nullptr)
    {
        CServerLog::Printf("[ERROR] : ENET_UDP_SERVER_SOCKET FAILED TO CREATE\n");
        return false;
    }

    pENetHost->intercept = LauncherQuery;
    CServerLog::Printf("[!] : Server started on port %d\n", port);

    CInternet::Start(port);

    g_simLatencyMs = CConfigManager::GetConfigSimLatencyMs();
    g_simJitterMs = CConfigManager::GetConfigSimJitterMs();
    g_simLossPercent = CConfigManager::GetConfigSimLossPercent();
    if (IsLagSimEnabled())
    {
        CServerLog::Printf("[!] : LAG SIMULATOR ON: latency %u ms, jitter +/-%u ms, loss %u%% (unreliable only)\n", g_simLatencyMs,
            g_simJitterMs, g_simLossPercent);
    }
    if (CConfigManager::GetConfigTestCommands())
    {
        CServerLog::Printf("[!] : Test commands ON (/traer, /caminar)\n");
    }

    ENetEvent eNetEvent{};
    while (true)  // waiting for event
    {
        CServerTime::Update();
        CRTTBroadcastManager::Update();
        CServerConsole::Process();
        CSaveManager::Process();
        CCoopMission::Process();
        CHandover::ProcessProximity();
        SimFlush();

        while (enet_host_service(pENetHost, &eNetEvent, 1) > 0)
        {
            switch (eNetEvent.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    CNetwork::HandlePeerConnected(eNetEvent);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(eNetEvent.peer);
                    if (pNetworkPlayer != nullptr)
                    {
                        GetPacketFactory().Receive(
                            eNetEvent.packet->data, eNetEvent.packet->dataLength, pNetworkPlayer);
                    }
                    else
                    {
                        GetPacketFactory().ReceivePacketNoAuth_ENet(
                            eNetEvent.packet->data, eNetEvent.packet->dataLength, eNetEvent.peer);
                    }

                    enet_packet_destroy(eNetEvent.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    CNetwork::HandlePlayerDisconnected(eNetEvent);
                    break;
                }
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }
    }

    enet_host_destroy(pENetHost);
    enet_deinitialize();
    CServerLog::Printf("[!] : Server Shutdown (ENET_DEINITIALIZE)\n");
    return 0;
}

void CNetwork::HandlePeerConnected(ENetEvent& event)
{
    CServerLog::Printf("[Game] : A new client connected from %i.%i.%i.%i:%u.\n", event.peer->address.host & 0xFF,
        (event.peer->address.host >> 8) & 0xFF, (event.peer->address.host >> 16) & 0xFF,
        (event.peer->address.host >> 24) & 0xFF, event.peer->address.port);

    enet_peer_timeout(event.peer, 0, 30000, 60000);  // timeoutLimit, timeoutMinimum, timeoutMaximum
}

void CNetwork::HandlePlayerDisconnected(ENetEvent& event)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(event.peer);

    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    // E: captured before the player is deleted (the mission abort depends on it)
    const bool bHostLeftOnMission = pNetworkPlayer->m_bIsHost && pNetworkPlayer->m_bOnMission;

    // F: while a coop side mission is active its owner, not the host, runs the script; a player that belongs to it
    // (owner or participant) is handled by COOP_MISSION_STATE below, not by E's host abort
    const bool bInCoopMission =
        CCoopMission::IsOwner(pNetworkPlayer) || CCoopMission::IsParticipant(pNetworkPlayer);

    // nothing may keep pointing at the player, it is deleted below
    for (auto pVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        for (auto& pOccupant : pVehicle->m_pPlayers)
        {
            if (pOccupant == pNetworkPlayer)
                pOccupant = nullptr;
        }
    }

    if (Packets::Scripts::g_pLastEnExPlayerOwner == pNetworkPlayer)
    {
        Packets::Scripts::g_pLastEnExPlayerOwner = nullptr;
    }

    // B1: give the peds/vehicles the leaving player hosted to a nearby player; the rest is removed as before
    CHandover::HandoverPedsFrom(pNetworkPlayer);
    CHandover::HandoverVehiclesFrom(pNetworkPlayer);

    // no ped/vehicle may keep pointing at the player while it is deleted below
    CHandover::ForgetPlayer(pNetworkPlayer);
    CModCheck::ForgetPlayer(pNetworkPlayer);

    CNetworkPlayerManager::Remove(pNetworkPlayer);

    // G2: the reason a player was disconnected by the server travels through enet's event data (the data passed to
    // enet_peer_disconnect[_later]); anything unknown falls back to "nothing"
    uint32_t disconnectReason = event.data;
    if (disconnectReason > Packets::System::PlayerDisconnected::DISCONNECTION_REASON_MODS_MISMATCH)
    {
        disconnectReason = Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NOTHING;
    }

    Packets::System::PlayerDisconnected playerDisconnected{};
    playerDisconnected.payload.SetPlayerId(pNetworkPlayer->m_playerId);
    playerDisconnected.payload.reason = (uint8_t)disconnectReason;
    GetPacketFactory().SendToAll(playerDisconnected);

    CServerLog::Printf("[Game] : %s Disconnected (reason %u).\n", to_string(pNetworkPlayer->m_playerId).c_str(),
        disconnectReason);

    // F: a coop side mission owns the script while it is active: the owner leaving closes it for its participants
    // (reason 4), and the host leaving is just one participant less
    CCoopMission::OnPlayerLeft(pNetworkPlayer);

    // the missions he was playing are free again
    CMissionLock::OnPlayerLeft(pNetworkPlayer);

    // E bug 6: he will not answer a save-all that is waiting for him
    CSaveManager::OnPlayerDisconnected(pNetworkPlayer);

    delete pNetworkPlayer;

    // E: a host that was in a coop mission aborts it for the others before anyone else takes over. F: if the leaving
    // host belonged to the coop side mission, that mission owns the script and COOP_MISSION_STATE (reason 4) already
    // told everyone what happened, so there is nothing to abort here
    const bool bAbortMission =
        bHostLeftOnMission && CConfigManager::GetConfigHostLeaveAbort() && !bInCoopMission;
    if (bAbortMission)
    {
        Packets::Scripts::MissionAborted missionAborted{};
        missionAborted.reason = Packets::Scripts::MissionAborted::HOST_LEFT;
        GetPacketFactory().SendToAll(missionAborted);
    }
    if (bHostLeftOnMission)
        CCoopMission::ClearLatePlayers();

    CNetworkPlayerManager::AssignHostToFirstPlayer();
}

// G2: the reason reaches HandlePlayerDisconnected through event.data; the player is told first (playerid -1: "it's
// you"), so his client knows why and does not try to reconnect
void CNetwork::KickPlayer(CNetworkPlayer* pNetworkPlayer, uint32_t reason)
{
    if (pNetworkPlayer == nullptr || pNetworkPlayer->m_pPeer == nullptr || pNetworkPlayer->m_bKickRequested)
    {
        return;
    }

    pNetworkPlayer->m_bKickRequested = true;

    Packets::System::PlayerDisconnected playerDisconnected{};
    playerDisconnected.payload.playerid = -1;
    playerDisconnected.payload.reason = (uint8_t)reason;
    GetPacketFactory().Send(playerDisconnected, pNetworkPlayer);

    enet_peer_disconnect_later(pNetworkPlayer->m_pPeer, reason);
}

void CNetwork::HandlePlayerConnected(ENetPeer* pENetPeer, Packets::System::PlayerConnected& playerConnected)
{
#ifndef _DEBUG
    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (strncmp(pNetworkPlayer->m_Name, playerConnected.payload.name, sizeof(pNetworkPlayer->m_Name)) == 0)
        {
            Packets::System::PlayerDisconnected playerDisconnected{};
            playerDisconnected.payload.playerid = -1;
            playerDisconnected.payload.reason = Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NAME_TAKEN;
            GetPacketFactory().SendPacketNoAuth_ENet(playerDisconnected, pENetPeer);
            enet_peer_disconnect_later(pENetPeer, 0);
            return;
        }
    }
#endif

    uint32_t packedVersion = semver_parse(COOPSA_VERSION, nullptr);
    char buffer[23];
    semver_t playerVersion;
    semver_unpack(playerConnected.payload.version, &playerVersion);
    semver_to_string(&playerVersion, buffer, sizeof(buffer));
    buffer[22] = '\0';

    if (packedVersion != playerConnected.payload.version)
    {
        Packets::System::PlayerDisconnected playerDisconnected{};
        playerDisconnected.payload.playerid = -1;
        playerDisconnected.payload.reason = Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH;
        playerDisconnected.payload.version = packedVersion;
        GetPacketFactory().SendPacketNoAuth_ENet(playerDisconnected, pENetPeer);
        enet_peer_disconnect_later(pENetPeer, packedVersion);
        return;
    }

    playerid_t newPlayerId = CNetworkPlayerManager::AllocatePlayerId();
    CNetworkPlayer* pNewNetworkPlayer = new CNetworkPlayer(pENetPeer, newPlayerId);
    strcpy_s(pNewNetworkPlayer->m_Name, playerConnected.payload.name);
    // a mission already in progress is not joined (docs/REQUISITOS.md 1): he gets none of it and plays the next one
    pNewNetworkPlayer->m_bLateForMission = CCoopMission::IsAuthorityOnMission();
    CNetworkPlayerManager::Add(pNewNetworkPlayer);

    logger::info("playerid %s name %s version %s", to_string(newPlayerId).c_str(), playerConnected.payload.name, buffer);

    // Send the NEW player TO OLD players
    playerConnected.payload.SetPlayerId(newPlayerId);
    GetPacketFactory().SendToAll(playerConnected, pNewNetworkPlayer);

    // Let the new player know his id
    Packets::System::PlayerHandshake playerHandshake{};
    playerHandshake.yourid = newPlayerId;
    GetPacketFactory().Send(playerHandshake, pNewNetworkPlayer);

    Packets::Voice::Settings voiceSettings{};
    voiceSettings.enabled = CConfigManager::GetVoiceChat();
    voiceSettings.range = CConfigManager::GetVoiceRange();
    GetPacketFactory().Send(voiceSettings, pNewNetworkPlayer);

    // server-client-config: the [clientes] switches win over each player's own coopandreas.ini (empty clears them)
    Packets::System::ClientConfigOverride clientConfig{};
    for (const auto& [key, value] : CConfigManager::GetClientOverrides())
    {
        if (clientConfig.count == Packets::System::ClientConfigOverride::MAX_ENTRIES ||
            key.size() >= Packets::System::ClientConfigOverride::KEY_SIZE)
            continue;
        strcpy_s(clientConfig.keys[clientConfig.count], key.c_str());
        clientConfig.values[clientConfig.count++] = value;
    }
    GetPacketFactory().Send(clientConfig, pNewNetworkPlayer);
    if (clientConfig.count > 0)
        logger::info("[clientes] %d interruptores impuestos a %s", clientConfig.count, playerConnected.payload.name);

    // the missions that are being played right now
    CMissionLock::SendSnapshot(pNewNetworkPlayer);

    // Send OLD players TO the NEW one
    Packets::System::PlayerConnected oldPlayerConnected{};
    oldPlayerConnected.payload.isAlreadyConnected = true;

    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_playerId == newPlayerId)
        {
            continue;
        }
        oldPlayerConnected.payload.SetPlayerId(pNetworkPlayer->m_playerId);
        strcpy_s(oldPlayerConnected.payload.name, pNetworkPlayer->m_Name);
        GetPacketFactory().Send(oldPlayerConnected, pNewNetworkPlayer);
    }

    for (auto i : CNetworkPlayerManager::m_pPlayers)
    {
        if (i->m_playerId == newPlayerId)
            continue;

        if (i->m_ucSyncFlags.bStatsModified)
        {
            Packets::Players::PlayerStats statsPacket{};
            statsPacket.senderPlayerid = i->m_playerId;
            for (size_t j = 0; j < ARRAY_SIZE(statsPacket.stats); j++)
            {
                statsPacket.stats[j] = i->m_afStats[j];
            }
            GetPacketFactory().Send(statsPacket, pNewNetworkPlayer);
        }

        if (i->m_ucSyncFlags.bClothesModified)
        {
            Packets::Players::RebuildPlayer rebuildPacket{};
            rebuildPacket.senderPlayerid = i->m_playerId;
            rebuildPacket.clothesDesc = i->m_pPedClothesDesc;
            GetPacketFactory().Send(rebuildPacket, pNewNetworkPlayer);
        }

        if (i->m_waypointState.place)
        {
            i->m_waypointState.senderPlayerid = i->m_playerId;
            GetPacketFactory().Send(i->m_waypointState, pNewNetworkPlayer);
        }
    }

    for (auto i : CNetworkVehicleManager::m_pVehicles)
    {
        Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
        vehicleSpawnPacket.vehicleid = i->m_vehicleId;
        vehicleSpawnPacket.modelid = i->m_nModelId;
        vehicleSpawnPacket.pos = i->m_vecPosition;
        vehicleSpawnPacket.rot = static_cast<float>(
            i->m_vecRotation.z * (3.141592f / 180.0f));  // convert to radians TODO(v0.3.1-alpha): is this wrong?
        vehicleSpawnPacket.color1 = i->m_nPrimaryColor;
        vehicleSpawnPacket.color2 = i->m_nSecondaryColor;
        // G4: the late joiner must know who owns the vehicle (a mission car, a parked one...) like everyone else did
        // when it was created, or it is treated as ambient traffic and may be duplicated / ghosted
        vehicleSpawnPacket.createdBy = static_cast<eVehicleCreatedBy>(i->m_nCreatedBy);
        GetPacketFactory().Send(vehicleSpawnPacket, pNewNetworkPlayer);

        Packets::Vehicles::VehicleDamage vehicleDamagePacket{};
        vehicleDamagePacket.vehicleid = i->m_vehicleId;
        vehicleDamagePacket.damageManager = i->m_damageManager;
        GetPacketFactory().Send(vehicleDamagePacket, pNewNetworkPlayer);

        for (int component : i->m_pComponents)
        {
            Packets::Vehicles::VehicleComponentAdd vehicleComponentAdd{};
            vehicleComponentAdd.vehicleid = i->m_vehicleId;
            vehicleComponentAdd.componentid = component;
            GetPacketFactory().Send(vehicleComponentAdd, pNewNetworkPlayer);
        }
    }

    for (auto i : CNetworkPedManager::m_pPeds)
    {
        Packets::Peds::PedSpawn packet{};
        packet.pedid = i->m_pedId;
        packet.modelId = i->m_nModelId;
        packet.pos = i->m_vecPos;
        packet.pedType = i->m_nPedType;
        packet.createdBy = i->m_nCreatedBy;
        packet.interiorPopulation = i->m_bInteriorPopulation;
        packet.areaCode = i->m_nAreaCode;
        snprintf(packet.specialModelName, sizeof(packet.specialModelName), "%s", i->m_szSpecialModelName);
        GetPacketFactory().Send(packet, pNewNetworkPlayer);
    }

    // the state this player had last time; its clothes go to everyone right away
    // (the client resends them after applying the state anyway)
    Packets::Players::PlayerPersistState persistState{};
    persistState.enabled = CPersistence::IsEnabled();
    if (persistState.enabled && CPersistence::Load(pNewNetworkPlayer->m_Name, persistState.data))
    {
        persistState.hasData = true;

        pNewNetworkPlayer->m_pPedClothesDesc = persistState.data.clothes;
        pNewNetworkPlayer->m_ucSyncFlags.bClothesModified = true;

        Packets::Players::RebuildPlayer rebuildPacket{};
        rebuildPacket.senderPlayerid = newPlayerId;
        rebuildPacket.clothesDesc = persistState.data.clothes;
        GetPacketFactory().SendToAll(rebuildPacket, pNewNetworkPlayer);

        logger::info("restored the saved state of %s", pNewNetworkPlayer->m_Name);
    }
    GetPacketFactory().Send(persistState, pNewNetworkPlayer);

    // F: a late joiner learns the coop side mission that is active (he is not a participant of it)
    CCoopMission::SendCurrentStateTo(pNewNetworkPlayer);
    SendGarageDoorsTo(pNewNetworkPlayer);  // T90

     if (Packets::Scripts::g_pLastEnExPlayerOwner)
     {
         if (std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(),
             Packets::Scripts::g_pLastEnExPlayerOwner) != CNetworkPlayerManager::m_pPlayers.end())
         {
             GetPacketFactory().Send(Packets::Scripts::g_lastEnExData, pNewNetworkPlayer);
         }
    }

    CNetworkPlayerManager::AssignHostToFirstPlayer();
    // AssignHostToFirstPlayer only announces a change: a late joiner must learn who the host already is (without it his
    // client knew no authority and dropped every mirrored mission blip: Ryder's and Sweet's blips and arrows)
    if (CNetworkPlayer* pHost = CNetworkPlayerManager::GetHost(); pHost && pHost != pNewNetworkPlayer)
    {
        Packets::System::PlayerAssignHost playerAssignHost{};
        playerAssignHost.playerid = pHost->m_playerId;
        GetPacketFactory().Send(playerAssignHost, pNewNetworkPlayer);
    }

    // G5: the new player is announced in everyone's chat, him included ("<nick> se conectó"); the client does not
    // print its own line anymore when `clear-messages` is on (coopandreas.ini)
    if (CConfigManager::GetConfigClearMessages())
    {
        Packets::System::ServerChat chat{};
        const std::string text = std::string(pNewNetworkPlayer->m_Name) + " se conecto";
        ServerTextToWide(text.c_str(), chat.message, ARRAY_SIZE(chat.message));
        GetPacketFactory().SendToAll(chat);
    }

    if (pNewNetworkPlayer->m_bLateForMission)
    {
        Packets::System::ServerChat chat{};
        ServerTextToWide("[Coop] Hay una mision en curso: vas a entrar en la proxima.", chat.message, ARRAY_SIZE(chat.message));
        GetPacketFactory().Send(chat, pNewNetworkPlayer);
    }
}

void CNetwork::SendPacketNoAuth_ENet(ENetPeer* pENetPeer, const uint8_t* data, int dataSize,
    ePacketChannel packetChannel, ePacketReliability packetReliability)
{
    uint32_t eNetPacketFlags = 0;
    if (packetReliability == ePacketReliability::RELIABLE)
    {
        eNetPacketFlags |= ENET_PACKET_FLAG_RELIABLE;
    }
    else if (packetReliability == ePacketReliability::UNRELIABLE)
    {
        // ...
    }

    ENetPacket* pENetPacket = enet_packet_create(data, dataSize, eNetPacketFlags);
    if (pENetPacket != nullptr)
    {
        if (IsLagSimEnabled())
        {
            SimSend(pENetPeer, static_cast<uint8_t>(packetChannel), pENetPacket,
                packetReliability == ePacketReliability::RELIABLE);
            if (pENetPacket->referenceCount == 0)
                enet_packet_destroy(pENetPacket);
            return;
        }
        enet_peer_send(pENetPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
    }
}

void CNetwork::SendPacket(CNetworkPlayer* pNetworkPlayer, const uint8_t* data, int dataSize,
    ePacketChannel packetChannel, ePacketReliability packetReliability)
{
    COOP_VERIFY(pNetworkPlayer != nullptr);

    uint32_t eNetPacketFlags = 0;
    if (packetReliability == ePacketReliability::RELIABLE)
    {
        eNetPacketFlags |= ENET_PACKET_FLAG_RELIABLE;
    }
    else if (packetReliability == ePacketReliability::UNRELIABLE)
    {
        // ...
    }

    ENetPacket* pENetPacket = enet_packet_create(data, dataSize, eNetPacketFlags);
    if (pENetPacket != nullptr)
    {
        if (IsLagSimEnabled())
        {
            SimSend(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket,
                packetReliability == ePacketReliability::RELIABLE);
            if (pENetPacket->referenceCount == 0)
                enet_packet_destroy(pENetPacket);
            return;
        }
        if (enet_peer_send(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket) < 0)
        {
            enet_packet_destroy(pENetPacket);
        }
    }
}

void CNetwork::SendPacketToAll(const uint8_t* data, int dataSize, ePacketChannel packetChannel,
    ePacketReliability packetReliability, CNetworkPlayer* pNetworkPlayerToIgnore)
{
    uint32_t eNetPacketFlags = 0;
    if (packetReliability == ePacketReliability::RELIABLE)
    {
        eNetPacketFlags |= ENET_PACKET_FLAG_RELIABLE;
    }
    else if (packetReliability == ePacketReliability::UNRELIABLE)
    {
        // ...
    }

    ENetPacket* pENetPacket = enet_packet_create(data, dataSize, eNetPacketFlags);
    if (pENetPacket != nullptr)
    {
        for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer != pNetworkPlayerToIgnore)
            {
                if (IsLagSimEnabled())
                {
                    SimSend(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket,
                        packetReliability == ePacketReliability::RELIABLE);
                    continue;
                }
                enet_peer_send(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
            }
        }

        // enet only frees packets it queued; if nobody received it, free it here
        if (pENetPacket->referenceCount == 0)
        {
            enet_packet_destroy(pENetPacket);
        }
    }
}
