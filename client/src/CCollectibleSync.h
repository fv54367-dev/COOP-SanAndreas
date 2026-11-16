#pragma once

#include "network/packets/world.h"

// Collectibles shared by the players in coop mission mode (F1): a tag sprayed or an oyster, horseshoe or snapshot
// collected by one counts for every other coop player (with the game's own reward and message). A player in individual
// mode neither sends nor receives them. Polled like CPickupSync (the code that collects them is partly protected in the
// 1.0 exe); a big jump at once (loaded game) only resets what is known.
class CCollectibleSync
{
public:
    static void Process();
    static void HandleTag(Packets::World::TagUpdate* packet);
    static void HandleCollectible(Packets::World::CollectiblePicked* packet);
};
