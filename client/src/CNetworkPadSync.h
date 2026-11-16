#pragma once

// 2-player minigames of the game (pool) read the second pad: while the host runs one of them as a coop mission,
// the second pad is the first network player's controller, so he plays his turns from his own game
class CNetworkPadSync
{
public:
    static void Init();
};
