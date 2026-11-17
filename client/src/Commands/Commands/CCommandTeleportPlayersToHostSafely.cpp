#include "stdafx.h"
#include "CCommandTeleportPlayersToHostSafely.h"
#include "COpCodeSync.h"
#include <CGeneral.h>
#include <CPedPlacement.h>
#include <CCoopMission.h>
#include <CMissionSyncRules.h>

static constexpr int MAX_TELEPORT_SLOTS = 3;
static constexpr float MIN_DIST = 1.0f;
static constexpr float MAX_DIST = 5.0f;
static void CalculateSafePositions(CVector& targetPos, float targetHeading, int playerCount, CVector* out) 
{
	COOP_VERIFY(playerCount <= MAX_TELEPORT_SLOTS);
	
	if (playerCount == 0) return;

	for (int attempt = 0, pedIndex = 0; attempt < 2 && pedIndex < playerCount; attempt++)
	{
		for (float dist = MIN_DIST; dist <= MAX_DIST && pedIndex < playerCount; dist += 0.5f)
		{
			for (float angle = targetHeading + 2.35619f; angle < targetHeading + 6.28319f && pedIndex < playerCount; angle += attempt == 0 ? 0.7f : 0.35f)
			{
				float rad = CGeneral::LimitRadianAngle(angle);
				CVector offset(-sinf(rad), cosf(rad), 0.0f);
				CVector teleportPos = targetPos + offset * dist;

				if (CPedPlacement::FindZCoorForPed(&teleportPos))
				{
					if (!CWorld::TestSphereAgainstWorld(teleportPos, 0.35f, false, true, true, true, true, true, false)
						&& CWorld::GetIsLineOfSightClear(targetPos, teleportPos, true, true, false, true, true, false, false))
					{
						out[pedIndex++] = teleportPos;
					}
				}
			}
		}
	}
}

void CCommandTeleportPlayersToHostSafely::FindSafeSpots(const CVector& target, float heading, int count, CVector* out)
{
	if (count > MAX_TELEPORT_SLOTS)
		count = MAX_TELEPORT_SLOTS;
	for (int i = 0; i < count; i++)
		out[i] = CVector(0.0f, 0.0f, 0.0f);
	CVector pos = target;
	CalculateSafePositions(pos, heading, count, out);
}

void CCommandTeleportPlayersToHostSafely::Process(CRunningScript* script)
{
	script->CollectParameters(3);

	COOP_VERIFY(CCoopMission::IsScriptAuthority());
	
	int playerCount = 0;
	CNetworkPlayer* networkPlayers[MAX_TELEPORT_SLOTS];
	for (int i = 0; i < MAX_TELEPORT_SLOTS; i++)
	{
		if (ScriptParams[i] != -1)
		{
			if (auto ped = CPools::GetPed(ScriptParams[i]))
			{
				if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(ped))
				{
					networkPlayers[playerCount] = networkPlayer;
					playerCount++;
				}
			}
		}
	}

	TeleportToHost(networkPlayers, playerCount);
}

void CCommandTeleportPlayersToHostSafely::TeleportToHost(CNetworkPlayer** networkPlayers, int playerCount)
{
	// a mission the local player runs alone (not the authority) must not move the others
	if (!CCoopMission::IsScriptAuthority()) return;

	if (playerCount == 0) return;

	CVector positions[MAX_TELEPORT_SLOTS];
	for (int i = 0; i < playerCount; i++)
		positions[i] = CVector(0.0f, 0.0f, 0.0f);

	CalculateSafePositions(FindPlayerCoors(0), FindPlayerPed(0)->m_fCurrentRotation, playerCount, positions);
	for (int i = 0; i < playerCount; i++)
	{
		if (positions[i].x == 0.0f && positions[i].y == 0.0f && positions[i].z == 0.0f)
		{
			positions[i] = FindPlayerCoors(0);
		}
		Packets::Scripts::TeleportPlayerScripted packet{};
		packet.playerid = networkPlayers[i]->m_playerId;
		packet.pos = positions[i];
		packet.heading = FindPlayerPed(0)->m_fCurrentRotation;
		// the ped's interior when he has one: a held script may not have made it visible yet (entry hold, 2026-11-12)
		packet.area = static_cast<unsigned char>(FindPlayerPed(0)->m_nAreaCode ? FindPlayerPed(0)->m_nAreaCode : CGame::currArea);
		GetPacketFactory().Send(packet);
	}
}


// every network player (up to 3, like the mission scripts), no parameters: saves mission script space
void CCommandTeleportAllNetworkPlayersToHost::Process(CRunningScript* script)
{
	if (!COpCodeSync::IsScriptSynced(script))
		return;  // a side mission the host plays individually

	// the C++ rule teleports them after the move of the host that comes right before (CMissionSyncRules)
	if (CMissionSyncRules::MutesTeleportCommand(script))
		return;

	CNetworkPlayer* networkPlayers[MAX_TELEPORT_SLOTS];
	int playerCount = 0;
	for (auto networkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (playerCount < MAX_TELEPORT_SLOTS && networkPlayer->m_pPed && CCoopMission::ShouldIncludeNetworkPlayer(networkPlayer))
			networkPlayers[playerCount++] = networkPlayer;
	}
	CCommandTeleportPlayersToHostSafely::TeleportToHost(networkPlayers, playerCount);
}
