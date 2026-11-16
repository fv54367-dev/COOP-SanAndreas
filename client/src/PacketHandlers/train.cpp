#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/train.h"
#include <CTrainSync.h>

// client side of train sync (TANDA_2026-11-10_glm_sync_trenes): everything lives in CTrainSync
PACKET_HANDLER(ePacketType::TRAIN_STATE, Packets::Trains::TrainState* pTrainState)
{
    CTrainSync::HandleState(pTrainState);
}

PACKET_HANDLER(ePacketType::TRAIN_EVENT, Packets::Trains::TrainEvent* pTrainEvent)
{
    switch (pTrainEvent->kind)
    {
    case Packets::Trains::TrainEvent::KIND_REMOVE:
        CTrainSync::HandleRemove(pTrainEvent);
        break;

    case Packets::Trains::TrainEvent::KIND_SPAWN:
    case Packets::Trains::TrainEvent::KIND_SNAPSHOT:  // idempotent: the same descriptors, a fresh client needs them
        CTrainSync::HandleSpawn(pTrainEvent);
        break;

    default:
        break;
    }
}
