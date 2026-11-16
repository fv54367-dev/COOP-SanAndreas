#pragma once

#include "network/packets/world.h"

// Weapons dropped by dead peds are created by the ped's syncer (or by whoever kills a local-only ped) and
// mirrored on every client; when a copy disappears (picked up or timed out) it is removed for everyone.
class CPickupSync
{
public:
    static void InjectHooks();
    static void Process();
    static void HandleSpawn(Packets::World::PickupSpawn* packet);
    static void HandleRemove(Packets::World::PickupRemove* packet);
};
