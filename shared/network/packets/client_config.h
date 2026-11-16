#pragma once

#include "network/packet.h"
#include "network/packet_types.h"
#include "serialize.h"

// server-client-config: the [clientes] section of the server's server-config.ini, sent to each player when he connects.
// Every switch in it wins over the player's own coopandreas.ini for this session (CConfigLoader::ApplyServerOverrides);
// his file keeps his own values. An empty list drops what a previous server imposed.
namespace Packets::System
{
class ClientConfigOverride : public Packet
{
    DEFINE_PACKET_TYPE(ClientConfigOverride, ePacketType::CLIENT_CONFIG_OVERRIDE, ePacketChannel::SYSTEM);

public:
    static constexpr int MAX_ENTRIES = 64;
    static constexpr int KEY_SIZE = 48;

    int count = 0;
    char keys[MAX_ENTRIES][KEY_SIZE]{};
    bool values[MAX_ENTRIES]{};

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && (count < 0 || count > MAX_ENTRIES))
            return false;
        serialize_int(stream, count, 0, MAX_ENTRIES);
        for (int i = 0; i < count; i++)
        {
            serialize_string(stream, keys[i], KEY_SIZE);
            serialize_bool(stream, values[i]);
        }
        return true;
    }
};
}  // namespace Packets::System
