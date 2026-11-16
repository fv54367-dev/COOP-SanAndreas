#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/train.h"

// Train sync (TANDA_2026-11-10_glm_sync_trenes): one authority only. The host's game simulates every train and
// everyone else mirrors it, so only the connection host may send TRAIN_*, and the server stores nothing: it just
// relays the state/event to everybody else. The host announces its snapshot on PLAYER_CONNECTED itself (broadcast),
// so no per-player targeting is needed here.

PACKET_HANDLER(ePacketType::TRAIN_STATE, Packets::Trains::TrainState* pTrainState, CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_bIsHost)
    {
        logger::warn("%s tried to send train state without being the authority", pNetworkPlayer->GetName().c_str());
        return;
    }

    GetPacketFactory().SendToAll(*pTrainState, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::TRAIN_EVENT, Packets::Trains::TrainEvent* pTrainEvent, CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_bIsHost)
    {
        logger::warn("%s tried to send a train event without being the authority", pNetworkPlayer->GetName().c_str());
        return;
    }

    GetPacketFactory().SendToAll(*pTrainEvent, pNetworkPlayer);
}
