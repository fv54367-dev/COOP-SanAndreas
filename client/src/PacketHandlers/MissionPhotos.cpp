#include "stdafx.h"
#include "CMissionPhotos.h"
#include "network/packets/mission_photos.h"
PACKET_HANDLER(ePacketType::MISSION_PHOTO, Packets::Scripts::MissionPhoto* packet)
{
    CMissionPhotos::Receive(*packet);
}
