#include "network/packet_types.h"
#include "network/packets/scripts.h"
#include "stdafx.h"
#include "CMissionLock.h"

PACKET_HANDLER(ePacketType::MISSION_LOCK_REQUEST, Packets::Scripts::MissionLockRequest* pRequest,
    CNetworkPlayer* pNetworkPlayer)
{
    CMissionLock::OnRequest(pNetworkPlayer, pRequest->missionId);
}

PACKET_HANDLER(ePacketType::MISSION_LOCK_RELEASE, Packets::Scripts::MissionLockRelease* pRelease,
    CNetworkPlayer* pNetworkPlayer)
{
    CMissionLock::OnRelease(pNetworkPlayer, pRelease->missionId);
}
