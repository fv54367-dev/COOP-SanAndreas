#pragma once
#include "network/packet.h"
#include "network/ids.h"

namespace Packets::Scripts
{
// The script authority waits for players of the mission (start marker or a marker inside it): every other player sees
// who is missing, or that they are the ones being waited for. Sent again every second while the wait lasts.
class MissionWaitNotice : public Packet
{
    DEFINE_PACKET_TYPE(MissionWaitNotice, ePacketType::MISSION_WAIT_NOTICE, ePacketChannel::SYNC);
public:
    enum Kind : uint8_t { START, MARKER, LEAVE_INTERIOR, ENTER_INTERIOR, BEACH, RACE_FINISH, KIND_COUNT };
    SenderPlayerId senderPlayerid{};
    uint8_t kind = START;
    char missing[128]{};  // names, ", " between them

    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_bits(stream, kind, 3);
        serialize_string(stream, missing, sizeof(missing));
        missing[sizeof(missing) - 1] = '\0';
        return true;
    }
};
}
