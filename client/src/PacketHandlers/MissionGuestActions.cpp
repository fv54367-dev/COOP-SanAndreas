#include "stdafx.h"
#include "CMissionGuestActions.h"
#include "network/packets/mission_guest_actions.h"
PACKET_HANDLER(ePacketType::MISSION_GUEST_ACTIONS, Packets::Scripts::MissionGuestActions* packet)
{
    CMissionGuestActions::Receive(*packet);
}
