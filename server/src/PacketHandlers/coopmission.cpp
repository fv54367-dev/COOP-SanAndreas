#include "network/packet_types.h"
#include "network/packets/scripts.h"
#include "stdafx.h"
#include "CCoopMission.h"

// F: cooperative side missions. The client that starts a side mission offers it here; the server reserves the single
// slot, invites everyone else and keeps the participant list (see CCoopMission).
PACKET_HANDLER(ePacketType::COOP_MISSION_OFFER, Packets::Scripts::CoopMissionOffer* pCoopMissionOffer,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    // the title is cut to its buffer by the packet itself; the script name must be a sane name
    pCoopMissionOffer->scriptName[ARRAY_SIZE(pCoopMissionOffer->scriptName) - 1] = '\0';
    pCoopMissionOffer->title[ARRAY_SIZE(pCoopMissionOffer->title) - 1] = '\0';

    CCoopMission::OnOffer(pNetworkPlayer, pCoopMissionOffer->scriptName, pCoopMissionOffer->title);
}

PACKET_HANDLER(ePacketType::COOP_MISSION_REPLY, Packets::Scripts::CoopMissionReply* pCoopMissionReply,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    // only an invited player has a say, and only while the offer is open
    CCoopMission::OnReply(pNetworkPlayer, pCoopMissionReply->accept);
}

PACKET_HANDLER(ePacketType::COOP_MISSION_LEAVE, Packets::Scripts::CoopMissionLeave* pCoopMissionLeave,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    // the owner leaving ends the mission, a participant leaving just drops out
    CCoopMission::OnLeave(pNetworkPlayer, pCoopMissionLeave->fell);
}

PACKET_HANDLER(ePacketType::COOP_MISSION_END, Packets::Scripts::CoopMissionEnd* pCoopMissionEnd,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CConfigManager::GetConfigCoopSideMissions())
        return;

    // only the owner can end his mission
    CCoopMission::OnEnd(pNetworkPlayer, pCoopMissionEnd->passed, pCoopMissionEnd->cancelled);
}
