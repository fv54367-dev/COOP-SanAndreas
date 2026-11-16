#include "network/packets/scripts.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CNetworkEntityBlip.h>
#include <CNetworkCheckpoint.h>
#include <CNetworkStaticBlip.h>
#include <CCoopMission.h>
#include <CMissionMirror.h>
#include <CMissionAreaMarkers.h>
#include <CMirrorRaceCheckpoints.h>
#include <CMissionMode.h>

PACKET_HANDLER(ePacketType::UPDATE_ENTITY_BLIP, Packets::Blips::UpdateEntityBlip* pUpdateEntityBlip)
{
	if (CCoopMission::IsScriptAuthority())
		return;

	CNetworkEntityBlip::UpdateEntityBlip(pUpdateEntityBlip);
}

PACKET_HANDLER(ePacketType::REMOVE_ENTITY_BLIP, Packets::Blips::RemoveEntityBlip* pRemoveEntityBlip)
{
	if (CCoopMission::IsScriptAuthority())
		return;

	CNetworkEntityBlip::RemoveEntityBlip(pRemoveEntityBlip);
}

PACKET_HANDLER(ePacketType::CLEAR_ENTITY_BLIPS, Packets::Blips::ClearEntityBlips* pClearEntityBlips)
{
	CNetworkEntityBlip::ClearEntityBlips();
}

PACKET_HANDLER(ePacketType::UPDATE_CHECKPOINT, Packets::Blips::UpdateCheckpoint* pUpdateCheckpoint)
{
	if (CCoopMission::IsScriptAuthority())
		return;

	CMissionMirror::LogEventf("rx-checkpoint pos=%.1f,%.1f,%.1f radius=%.1f", pUpdateCheckpoint->position.x,
		pUpdateCheckpoint->position.y, pUpdateCheckpoint->position.z, pUpdateCheckpoint->radius.x);
	CNetworkCheckpoint::Update(pUpdateCheckpoint->position, pUpdateCheckpoint->radius);
}

PACKET_HANDLER(ePacketType::REMOVE_CHECKPOINT, Packets::Blips::RemoveCheckpoint* pRemoveCheckpoint)
{
	if (CCoopMission::IsScriptAuthority())
		return;

	CMissionMirror::LogEventf("rx-checkpoint-remove");
	CNetworkCheckpoint::Remove();
}

PACKET_HANDLER(ePacketType::MISSION_AREA_MARKER, Packets::Blips::MissionAreaMarker* pMissionAreaMarker)
{
	if (CMissionMode::IsIndividual() && !CCoopMission::IsScriptAuthority())
		return;
	CMissionAreaMarkers::Handle(pMissionAreaMarker);
}

PACKET_HANDLER(ePacketType::MISSION_RACE_CHECKPOINTS, Packets::Blips::MissionRaceCheckpoints* pRaceCheckpoints)
{
	if (CMissionMode::IsIndividual() && !CCoopMission::IsScriptAuthority())
		return;
	CMirrorRaceCheckpoints::Handle(pRaceCheckpoints);
}

PACKET_HANDLER(ePacketType::CREATE_STATIC_BLIP, Packets::Blips::StaticBlipsSnapshot* pCreateStaticBlip)
{
	if (CLocalPlayer::m_bIsHost)
	{
		return;
	}

	// the coordinate blips (the mission's targets) of the host's snapshot, to compare with what the guest shows
	char coords[160]{};
	int nCoords = 0;
	for (size_t i = 0; i < pCreateStaticBlip->countBlips; i++)
	{
		const auto& blip = pCreateStaticBlip->blips[i];
		if (!blip.type)
			continue;

		if (nCoords < 6)
		{
			const size_t len = strlen(coords);
			sprintf_s(coords + len, sizeof(coords) - len, "%.0f,%.0f/c%u ", blip.position.x, blip.position.y,
				(unsigned)blip.color);
		}
		++nCoords;
	}
	CNetworkStaticBlip::Create(*pCreateStaticBlip);
	// own=1: this game kept its own icons and only made the host's mission blips (made=N)
	CMissionMirror::LogEventf("rx-static-blips n=%u coords=%d own=%d made=%d %s", (unsigned)pCreateStaticBlip->countBlips, nCoords,
		CConfigLoader::ms_bOwnStaticBlips && CConfigLoader::ms_bVanillaScripts ? 1 : 0, (int)CNetworkStaticBlip::ms_aSnapshotHandles.size(), coords);
}
