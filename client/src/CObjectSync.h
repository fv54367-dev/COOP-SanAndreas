#pragma once

#include "network/packets/world.h"
#include <vector>
#include <utility>
class CObject;

// Objects created by the host's coop mission (crates, props, doors, barriers...) only existed on the host.
// The host sends every mission object while a synced mission runs (created/moved/rotated/hidden/removed,
// checked every 100 ms) and the others keep static copies of them.
class CObjectSync
{
public:
    static std::vector<std::pair<uint32_t, CObject*>> GetPhotoTargets(bool owned);
    static uint32_t GetNetworkId(int handle);
    static CObject* Resolve(uint32_t id);
    static void Process();
    // object-state-smooth: per frame, moves the copies from the previous packet's position to the new one
    static void FrameUpdate();
    static void ResendAll();  // host: a player connected
    static void HandleSync(Packets::World::ObjectSync* pPacket);
    static void HandleRemove(Packets::World::ObjectRemove* pPacket);
};
