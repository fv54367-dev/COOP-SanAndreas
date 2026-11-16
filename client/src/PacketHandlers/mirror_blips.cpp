// Ported from project_gta_sa_coop (sametozbalkan, GPL-3.0), package M2, and adapted.
#include "network/packets/mirror_blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMirrorBlips.h>

// the mission mirror layer's blips from the script authority (CMirrorBlips.h)
PACKET_HANDLER(ePacketType::MISSION_MIRROR_BLIPS, Packets::Mirror::MirrorBlips* pMirrorBlips)
{
    CMirrorBlips::HandlePacket(*pMirrorBlips);
}
