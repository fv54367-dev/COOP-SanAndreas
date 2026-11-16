#include "network/packets/world.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CWeatherSync.h>
#include <CMoonSync.h>
#include <game_sa/CTagManager.h>
#include <CPickupSync.h>
#include <CCollectibleSync.h>
#include "CMissionMode.h"
#include <CFireSync.h>
#include <CObjectSync.h>
#include <CCheatSync.h>
#include <CProjectileInfo.h>
#include <CProjectile.h>

PACKET_HANDLER(ePacketType::CHEAT_SYNC, Packets::World::CheatSync* pCheatSync)
{
    CCheatSync::HandlePacket(pCheatSync);
}

PACKET_HANDLER(ePacketType::CHEAT_ANNOUNCE, Packets::World::CheatAnnounce* pAnnounce)
{
    CCheatSync::HandleAnnounce(pAnnounce);
}

PACKET_HANDLER(ePacketType::OBJECT_SYNC, Packets::World::ObjectSync* pObjectSync)
{
    CObjectSync::HandleSync(pObjectSync);
}

PACKET_HANDLER(ePacketType::OBJECT_REMOVE, Packets::World::ObjectRemove* pObjectRemove)
{
    CObjectSync::HandleRemove(pObjectRemove);
}

PACKET_HANDLER(ePacketType::FIRE_START, Packets::World::FireStart* pFireStart)
{
    CFireSync::HandleStart(pFireStart);
}

PACKET_HANDLER(ePacketType::FIRE_REMOVE, Packets::World::FireRemove* pFireRemove)
{
    CFireSync::HandleRemove(pFireRemove);
}

PACKET_HANDLER(ePacketType::PICKUP_SPAWN, Packets::World::PickupSpawn* pPickupSpawn)
{
    CPickupSync::HandleSpawn(pPickupSpawn);
}

PACKET_HANDLER(ePacketType::PICKUP_REMOVE, Packets::World::PickupRemove* pPickupRemove)
{
    CPickupSync::HandleRemove(pPickupRemove);
}

PACKET_HANDLER(ePacketType::GAME_WEATHER_TIME, Packets::World::GameWeatherTime* pGameWeatherTime)
{
	CWeatherSync::HandlePacket(pGameWeatherTime);
}

PACKET_HANDLER(ePacketType::ADD_EXPLOSION, Packets::World::AddExplosion* pAddExplosion)
{
	//unused for now
	//CExplosion::AddExplosion(nullptr, nullptr, pAddExplosion->type, pAddExplosion->pos, pAddExplosion->time, pAddExplosion->usesSound, pAddExplosion->cameraShake, pAddExplosion->isVisible);
}

// the owner's client detonated his satchels: blow up our copies the same way the owner's game did
// (CProjectileInfo::RemoveDetonatorProjectiles, 0x738860), but only the charges of that creator.
// Also used by the CWorld::UseDetonator hook (WorldHooks.cpp) so a local detonation does not blow up the
// charges of the other players.
void DetonateSatchelsOf(CEntity* creator)
{
	if (creator == nullptr || !creator->IsVTableValid())
	{
		return;
	}

	for (unsigned int i = 0; i < MAX_PROJECTILE_INFOS; i++)
	{
		CProjectileInfo& info = gaProjectileInfo[i];

		if (!info.m_bActive || info.m_pCreator != creator || info.m_nWeaponType != WEAPON_SATCHEL_CHARGE)
		{
			continue;
		}

		CProjectile* projectile = CProjectileInfo::ms_apProjectile[i];
		if (projectile == nullptr)
		{
			continue;
		}

		CVector pos = projectile->GetPosition();
		plugin::CallAndReturn<bool, 0x736A50, CEntity*, CEntity*, eExplosionType, CVector, unsigned int, unsigned char,
			float, unsigned char>(nullptr, creator, EXPLOSION_GRENADE, pos, 0, true, -1.0f, false);

		info.m_bActive = false;
		if (info.m_pFxSystem)
		{
			info.m_pFxSystem->Kill();
			info.m_pFxSystem = nullptr;
		}

		// the charge is spent, let the game take the copy out of the world before it detonates again
		projectile->m_bRemoveFromWorld = true;
	}
}

PACKET_HANDLER(ePacketType::DETONATE_PROJECTILES, Packets::World::DetonateProjectiles* pDetonateProjectiles)
{
	DetonateSatchelsOf(pDetonateProjectiles->creator.GetEntity());
}

PACKET_HANDLER(ePacketType::TAG_UPDATE, Packets::World::TagUpdate* pTagUpdate)
{
    CCollectibleSync::HandleTag(pTagUpdate);
}

PACKET_HANDLER(ePacketType::COLLECTIBLE_PICKED, Packets::World::CollectiblePicked* pCollectiblePicked)
{
    CCollectibleSync::HandleCollectible(pCollectiblePicked);
}

PACKET_HANDLER(ePacketType::UPDATE_ALL_TAGS, Packets::World::UpdateAllTags* pUpdateAllTags)
{
	// tags are only shared between players in coop mission mode (CCollectibleSync)
	if (CMissionMode::IsIndividual())
		return;

	// a hack to not draw "TAGS SPRAYED 1 of 100", look at 0x49CF4B
	bool saved = TheCamera.m_bWideScreenOn;
	TheCamera.m_bWideScreenOn = true;
	for (auto& tag : pUpdateAllTags->tags)
	{
		for (auto& tagDesc : CTagManager::ms_tagDesc)
		{
			if (!tagDesc.m_pEntity)
			{
				continue;
			}

			auto& pos = tagDesc.m_pEntity->GetPosition();

			if (static_cast<int16_t>(floor(pos.x)) == tag.pos_x &&
				static_cast<int16_t>(floor(pos.y)) == tag.pos_y &&
				static_cast<int16_t>(floor(pos.z)) == tag.pos_z)
			{
				CTagManager::SetAlpha(tagDesc.m_pEntity, tag.alpha);
			}
		}
	}
	TheCamera.m_bWideScreenOn = saved;
}

PACKET_HANDLER(ePacketType::UPDATE_MOON_SIZE, Packets::World::UpdateMoonSize* pUpdateMoonSize)
{
	CMoonSync::HandlePacket(pUpdateMoonSize);
}

#include <CGarageSync.h>
PACKET_HANDLER(ePacketType::GARAGE_DOOR, Packets::World::GarageDoor* pGarageDoor)
{
    CGarageSync::HandlePacket(*pGarageDoor);
}
