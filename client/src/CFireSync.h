#pragma once

#include "network/packets/world.h"

// Fire sync. Every fire has an owner: the client where it started announces it (FIRE_START) and the others create a
// copy that doesn't spread (spreading is announced by the owner as new fires). A copy that gets extinguished asks the
// owner to put the original out; when the original ends, every copy is removed (FIRE_REMOVE).
// A fire announced next to an existing local fire (e.g. the same car explosion on both clients) is linked to it
// instead of creating a second one.
class CFireSync
{
public:
    static void Process();
    static void HandleStart(Packets::World::FireStart* packet);
    static void HandleRemove(Packets::World::FireRemove* packet);
};
