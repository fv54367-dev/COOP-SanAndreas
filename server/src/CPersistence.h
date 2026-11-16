#pragma once

#include <network/packets/players.h>

// per player state saved on disk (saves/<nickname>.bin), see Packets::Players::SPersistData
class CPersistence
{
public:
    static bool IsEnabled();
    static bool Load(const char* name, Packets::Players::SPersistData& data);
    static void Save(const char* name, const Packets::Players::SPersistData& data);
};
