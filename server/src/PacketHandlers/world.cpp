#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/world.h"
#include "CCoopMission.h"
#include <map>

PACKET_HANDLER(ePacketType::FIRE_START, Packets::World::FireStart* pFireStart, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pFireStart, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::FIRE_REMOVE, Packets::World::FireRemove* pFireRemove, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pFireRemove, pNetworkPlayer);
}

// mission objects only come from the script authority (the coop mission owner while it runs)
PACKET_HANDLER(ePacketType::OBJECT_SYNC, Packets::World::ObjectSync* pObjectSync, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
        CCoopMission::Relay(*pObjectSync, pNetworkPlayer, pNetworkPlayer, true);
}

PACKET_HANDLER(ePacketType::OBJECT_REMOVE, Packets::World::ObjectRemove* pObjectRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (CCoopMission::IsScriptAuthority(pNetworkPlayer))
        CCoopMission::Relay(*pObjectRemove, pNetworkPlayer, pNetworkPlayer, true);
}

PACKET_HANDLER(ePacketType::PICKUP_SPAWN, Packets::World::PickupSpawn* pPickupSpawn, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pPickupSpawn, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PICKUP_REMOVE, Packets::World::PickupRemove* pPickupRemove, CNetworkPlayer* pNetworkPlayer)
{
    GetPacketFactory().SendToAll(*pPickupRemove, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::GAME_WEATHER_TIME, Packets::World::GameWeatherTime* pGameWeatherTime, CNetworkPlayer* pNetworkPlayer)
{
	if (!pNetworkPlayer->m_bIsHost)
	{
		return;
	}

	GetPacketFactory().SendToAll(*pGameWeatherTime, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::ADD_EXPLOSION, Packets::World::AddExplosion* pAddExplosion, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pAddExplosion, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::DETONATE_PROJECTILES, Packets::World::DetonateProjectiles* pDetonateProjectiles,
    CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pDetonateProjectiles, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::TAG_UPDATE, Packets::World::TagUpdate* pTagUpdate, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pTagUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::COLLECTIBLE_PICKED, Packets::World::CollectiblePicked* pCollectiblePicked,
    CNetworkPlayer* pNetworkPlayer)
{
    pCollectiblePicked->playerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pCollectiblePicked, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::UPDATE_ALL_TAGS, Packets::World::UpdateAllTags* pUpdateAllTags, CNetworkPlayer* pNetworkPlayer)
{
	if (pNetworkPlayer->m_bIsHost)
	{
		GetPacketFactory().SendToAll(*pUpdateAllTags, pNetworkPlayer);
	}
}

PACKET_HANDLER(ePacketType::UPDATE_MOON_SIZE, Packets::World::UpdateMoonSize* pUpdateMoonSize, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pUpdateMoonSize, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::CHEAT_SYNC, Packets::World::CheatSync* pCheatSync, CNetworkPlayer* pNetworkPlayer)
{
	GetPacketFactory().SendToAll(*pCheatSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::CHEAT_ANNOUNCE, Packets::World::CheatAnnounce* pAnnounce, CNetworkPlayer* pNetworkPlayer)
{
	pAnnounce->senderPlayerid = pNetworkPlayer->m_playerId;
	GetPacketFactory().SendToAll(*pAnnounce, pNetworkPlayer);
}

// Last state of each garage, replayed to late joiners. Individual clients show mission doors without applying their script state.
static std::map<uint8_t, Packets::World::GarageDoor> s_garageDoors;

PACKET_HANDLER(ePacketType::GARAGE_DOOR, Packets::World::GarageDoor* pGarageDoor, CNetworkPlayer* pNetworkPlayer)
{
	if (pNetworkPlayer->m_bIndividualMode && !pGarageDoor->visualOnly)
		return;
	s_garageDoors[pGarageDoor->index] = *pGarageDoor;
	for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (!pGarageDoor->visualOnly || pPlayer != pNetworkPlayer)
			GetPacketFactory().Send(*pGarageDoor, pPlayer);
	}
}

void SendGarageDoorsTo(CNetworkPlayer* pNewPlayer)
{
	if (CNetworkPlayerManager::m_pPlayers.size() <= 1)
	{
		s_garageDoors.clear();  // nobody else is playing: a new world starts from its own doors
		return;
	}
	for (auto& [index, packet] : s_garageDoors)
		GetPacketFactory().Send(packet, pNewPlayer);
}
