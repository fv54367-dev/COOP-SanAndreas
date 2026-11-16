#include "network/packets/scripts.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CCoopMission.h"

// F: cooperative side missions. The server invites every other player when someone offers a coop mission and broadcasts
// the mission state to everyone; CCoopMission is the single owner of that state on this client.
PACKET_HANDLER(ePacketType::COOP_MISSION_INVITE, Packets::Scripts::CoopMissionInvite* pCoopMissionInvite)
{
    CCoopMission::OnInvite(*pCoopMissionInvite);
}

PACKET_HANDLER(ePacketType::COOP_MISSION_STATE, Packets::Scripts::CoopMissionState* pCoopMissionState)
{
    CCoopMission::OnState(*pCoopMissionState);
}
