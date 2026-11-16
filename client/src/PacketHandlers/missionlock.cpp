#include "network/packets/scripts.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CMissionLock.h>

PACKET_HANDLER(ePacketType::MISSION_LOCK_STATE, Packets::Scripts::MissionLockState* pState)
{
    CMissionLock::HandleState(pState->missionId, pState->locked, pState->ownerId);
}
