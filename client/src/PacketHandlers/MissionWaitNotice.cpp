#include "stdafx.h"
#include "CMissionHints.h"
#include "network/packets/mission_wait_notice.h"
PACKET_HANDLER(ePacketType::MISSION_WAIT_NOTICE, Packets::Scripts::MissionWaitNotice* packet)
{
    CMissionHints::ReceiveWaitNotice(packet->senderPlayerid.value, packet->kind, packet->missing);
}
