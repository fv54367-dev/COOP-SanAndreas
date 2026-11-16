#pragma once

#include <cstdint>
#include <vector>

class CNetworkPlayer;
namespace Packets::Players
{
class StorySaveChunk;
}

// the host's last savegame (saves/story.b), the launcher downloads it and the game loads it before connecting
class CStorySave
{
public:
    static void Load();
    static void HandleChunk(CNetworkPlayer* pNetworkPlayer, Packets::Players::StorySaveChunk* pChunk);

    static inline std::vector<uint8_t> ms_data;
    static inline uint32_t ms_nCrc = 0;

private:
    static inline std::vector<uint8_t> ms_incoming;
    static inline uint32_t ms_nIncomingReceived = 0;
};

class CServerQuery
{
public:
    static int ENET_CALLBACK Intercept(ENetHost* host, ENetEvent* event);
};
