#include "stdafx.h"
#include "CHomeInvasion.h"
#include "network/packets/home_invasion.h"
PACKET_HANDLER(ePacketType::HOME_INVASION, Packets::Scripts::HomeInvasion* packet)
{
    CHomeInvasion::Receive(*packet);
}
