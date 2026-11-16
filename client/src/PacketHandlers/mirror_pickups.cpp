#include "network/packets/mirror_pickups.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorPickups.h>

// the mission mirror layer's pickups (CMirrorPickups.h)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_PICKUP, Packets::Mirror::MirrorPickup* pMirrorPickup)
{
    CMirrorPickups::HandlePacket(*pMirrorPickup);
}
