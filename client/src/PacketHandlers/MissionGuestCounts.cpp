#include "stdafx.h"
#include "CMissionGuestCounts.h"
#include "network/packets/mission_guest_counts.h"
PACKET_HANDLER(ePacketType::MISSION_GUEST_COUNTS, Packets::Scripts::MissionGuestCounts* packet)
{
    CMissionGuestCounts::Receive(*packet);
}
