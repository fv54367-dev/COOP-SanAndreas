#include "CModCheck.h"
#include "CEntryExitMarkerSync.h"
#include "CNetworkStaticBlip.h"
#include "COpCodeSync.h"
#include "CMissionMode.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "network/packets/client_config.h"
#include "CMissionMirror.h"
#include "stdafx.h"
#include <CWeatherSync.h>
#include <CMoonSync.h>
#include <CObjectSync.h>
#include <CCheatSync.h>
#include <CGangSync.h>
#include <CMissionMirror.h>
#include <CCoopMission.h>
#include <CTrainSync.h>
#include <game_sa/CTagManager.h>

PACKET_HANDLER(ePacketType::PLAYER_CONNECTED, Packets::System::PlayerConnected* pPlayerConnected)
{
    CNetworkPlayer* pNetworkPlayer =
        new CNetworkPlayer(pPlayerConnected->payload.GetPlayerId(), CVector(2246.506f, -1259.552f, 23.9531f));
    CNetworkPlayerManager::Add(pNetworkPlayer);

    strcpy_s(pNetworkPlayer->m_Name, pPlayerConnected->payload.name);

    pNetworkPlayer->m_bHasBeenConnectedBeforeMe = pPlayerConnected->payload.isAlreadyConnected;

    // a mission already in progress is not joined (docs/REQUISITOS.md 1): the server relays none of it to him and the
    // local mission doesn't wait for him nor fails with him
    if (!pNetworkPlayer->m_bHasBeenConnectedBeforeMe && CCoopMission::IsScriptAuthority() && CTheScripts::OnAMissionFlag &&
        CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] && COpCodeSync::IsSyncedMissionRunning())
    {
        pNetworkPlayer->m_bLateForMission = true;
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} " + pNetworkPlayer->GetName() + " entra en la proxima mision.");
    }

    if (!pNetworkPlayer->m_bHasBeenConnectedBeforeMe && !CConfigLoader::ms_bClearMessages)
    {
        CChat::AddMessage("{c7a6ff}[Player] {d6dce8}" + pNetworkPlayer->GetName() + " se conecto");
    }
    // the newcomer learns how the local player plays the missions
    CMissionMode::SendLocal();

    // the mission view (text table, objective, help box) for the newcomer
    if (!pNetworkPlayer->m_bHasBeenConnectedBeforeMe && CCoopMission::IsScriptAuthority())
        CMissionMirror::OnPlayerJoined(pPlayerConnected->payload.GetPlayerId());

    // TODO REFACTOR
    if (CLocalPlayer::m_bIsHost)
    {
        CTrainSync::SendSnapshot();  // the newcomer (and everyone) gets the host's trains as they are
        CObjectSync::ResendAll();
        CNetworkStaticBlip::ms_bNeedToSendAfterThisFrame = true;
        CEntryExitMarkerSync::ms_bUpdateAfterProcessingThisFrame = true;
        CTagSync::SyncCurrentState();
        CMoonSync::SyncCurrentState();
        CCheatSync::SyncCurrentState();
        CGangSync::SyncCurrentState();
        CWeatherSync::SyncCurrentState();  // G4: the newcomer must get the current weather right away

        // the newcomer must know if the host is already on a mission
        if (CTheScripts::OnAMissionFlag != 0)
        {
            Packets::Scripts::OnMissionFlagSync packet{};
            packet.bOnMission = CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag] &&
                                COpCodeSync::IsSyncedMissionRunning();
            GetPacketFactory().Send(packet);
        }
    }
}

PACKET_HANDLER(ePacketType::PLAYER_DISCONNECTED, Packets::System::PlayerDisconnected* pPlayerDisconnected)
{
    if (pPlayerDisconnected->payload.playerid == -1)
    {
        if (pPlayerDisconnected->payload.reason ==
            Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH)
        {
            char buffer[23];
            semver_t expected;
            semver_unpack(pPlayerDisconnected->payload.version, &expected);
            semver_to_string(&expected, buffer, sizeof buffer);
            buffer[22] = '\0';
            CChat::AddMessage("{63c7e8}[Network] {e8b86d}Version distinta, servidor: %s cliente: %s", buffer,
                COOPSA_VERSION);
        }
        else if (pPlayerDisconnected->payload.reason ==
                 Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NAME_TAKEN)
        {
            CChat::AddMessage("{63c7e8}[Network] {e8b86d}El nick '%s' ya esta en uso.", CLocalPlayer::m_Name);
        }
        else if (pPlayerDisconnected->payload.reason ==
                 Packets::System::PlayerDisconnected::DISCONNECTION_REASON_INVALID_PACKETS)
        {
            CChat::AddMessage("{63c7e8}[Network] {e8b86d}El servidor te echo por mandar paquetes invalidos.");
        }
        else if (pPlayerDisconnected->payload.reason ==
                 Packets::System::PlayerDisconnected::DISCONNECTION_REASON_KICKED)
        {
            CChat::AddMessage("{63c7e8}[Network] {e8b86d}El servidor te echo.");
        }
        else if (pPlayerDisconnected->payload.reason ==
                 Packets::System::PlayerDisconnected::DISCONNECTION_REASON_MODS_MISMATCH)
        {
            CChat::AddMessage("{63c7e8}[Network] {e8b86d}Desconectado: tus mods no coinciden con los del host (arriba dice cuales).");
        }
        CNetwork::Disconnect();
        return;
    }

    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(pPlayerDisconnected->payload.GetPlayerId());
    if (player != nullptr)
    {
        std::string playerName = player->GetName();
        CNetworkPlayerManager::Remove(player);
        delete player;

        std::string reason;
        switch (pPlayerDisconnected->payload.reason)
        {
            case Packets::System::PlayerDisconnected::DISCONNECTION_REASON_INVALID_PACKETS:
                reason = " (paquetes invalidos)";
                break;
            case Packets::System::PlayerDisconnected::DISCONNECTION_REASON_KICKED:
                reason = " (expulsado)";
                break;
            case Packets::System::PlayerDisconnected::DISCONNECTION_REASON_MODS_MISMATCH:
                reason = " (mods distintos al host)";
                break;
            default:
                break;
        }

        CChat::AddMessage("{c7a6ff}[Player] {d6dce8}" + playerName + " se desconecto" + reason);
    }
}

PACKET_HANDLER(ePacketType::PLAYER_HANDSHAKE, Packets::System::PlayerHandshake* pPlayerHandshake)
{
    if (!pPlayerHandshake->yourid.valid())
        return;

    CNetworkPlayerManager::m_localPlayerId = pPlayerHandshake->yourid;

    // G3: the counters are already fresh at the first handshake; after an auto-reconnect they must not be reset or
    // the entities this client hosts (which keep their previous ids) would collide with the newly allocated ones
    static bool bFirstHandshake = true;
    if (bFirstHandshake)
    {
        bFirstHandshake = false;
        CNetworkVehicleManager::ResetIdAllocator();
        CNetworkPedManager::ResetIdAllocator();
    }

    CNetwork::m_nAuthenticatedSessionId = CNetwork::m_nSessionId;
    CNetwork::m_bAuthenticated = true;
    CStatsSync::NotifyChanged();
    CStatsSync::ResetClothes();
    CPersistenceSync::Reset();
    CPatch::RevertTemporaryPatches();
    CModCheck::SendManifest();
    logger::info("Authenticated, playerid %s", to_string(pPlayerHandshake->yourid).c_str());
}

PACKET_HANDLER(ePacketType::RTT_BROADCAST, Packets::System::RTTBroadcast* pRTTBroadcast)
{
    for (int nRTTIndex = 0; nRTTIndex < pRTTBroadcast->playerCount; nRTTIndex++)
    {
        Packets::System::RTTBroadcast::SPlayerRTT& playerRTT = pRTTBroadcast->rtts[nRTTIndex];
        if (playerRTT.playerid == CNetworkPlayerManager::m_localPlayerId)
        {
            continue;
        }

        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(playerRTT.playerid);
        if (pNetworkPlayer != nullptr)
        {
            pNetworkPlayer->m_nRTT = playerRTT.rtt;
        }
    }
}

PACKET_HANDLER(ePacketType::PLAYER_ASSIGN_HOST, Packets::System::PlayerAssignHost* pPlayerAssignHost)
{
    if (pPlayerAssignHost->playerid == CNetworkPlayerManager::m_localPlayerId)
    {
        CLocalPlayer::m_bIsHost = true;

        CPatch::RevertTemporaryPatchesForHost();  // TODO is this needed?
        CTrainSync::OnBecameHost();

        CNetworkPedManager::AssignHost();
        CWeatherSync::SyncCurrentState();
        CTagSync::SyncCurrentState();
        CChat::AddMessage("{c7a6ff}[Player] {d6dce8}Ahora sos el host");
        return;
    }

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_playerId == pPlayerAssignHost->playerid)
        {
            CChat::AddMessage("{c7a6ff}[Player] {d6dce8}" + pNetworkPlayer->GetName() + " es el nuevo host");
            pNetworkPlayer->m_bIsHost = true;
        }
        else
        {
            pNetworkPlayer->m_bIsHost = false;
        }
    }

    CLocalPlayer::m_bIsHost = false;
    CTrainSync::OnBecameFollower();  // drop own random trains and any leftover copy state
}

PACKET_HANDLER(ePacketType::PLAYER_CHAT_MESSAGE, Packets::System::ChatMessage* pChatMessage)
{
    CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(pChatMessage->senderPlayerid);

    if (player)
    {
        CChat::AddPlayerChatMessage(player->GetName().c_str(), pChatMessage->message);
    }
}

PACKET_HANDLER(ePacketType::SERVER_CHAT, Packets::System::ServerChat* pServerChat)
{
    CChat::AddMessageRich(std::wstring(L"{c7a6ff}[Server]{d6dce8} ") + pServerChat->message, true);
}

// server-client-config: the server's [clientes] switches win over this game's coopandreas.ini for the session
PACKET_HANDLER(ePacketType::CLIENT_CONFIG_OVERRIDE, Packets::System::ClientConfigOverride* packet)
{
    std::vector<std::pair<std::string, bool>> overrides;
    for (int i = 0; i < packet->count && i < Packets::System::ClientConfigOverride::MAX_ENTRIES; i++)
    {
        packet->keys[i][Packets::System::ClientConfigOverride::KEY_SIZE - 1] = '\0';
        overrides.emplace_back(packet->keys[i], packet->values[i]);
    }
    CConfigLoader::ApplyServerOverrides(overrides);
    CMissionMirror::LogEventf("server-client-config n=%d", static_cast<int>(overrides.size()));
    if (!overrides.empty())
        CChat::AddMessage("{c7a6ff}[Coop]{d6dce8} El server fija " + std::to_string(overrides.size()) +
                          " opciones del mod para todos.");
}
