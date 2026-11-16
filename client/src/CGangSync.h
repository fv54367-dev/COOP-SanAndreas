#pragma once

#include "stdafx.h"

namespace Packets::Gangs
{
class GangZonesSync;
class GangWarState;
}  // namespace Packets::Gangs

// Host: mirrors the territories (CZoneInfo array) and the gang war state (CGangWars) to the other players.
// Clients: apply the host's CZoneInfo array (and keep re-applying it if a local script changes it) and
// mirror the war notice/blip. Their own CGangWars::Update call is skipped (hook on 0x53C122).
//
// Init() is called from CCore::Init, next to CCheatMenu::Init().
class CGangSync
{
public:
    static void Init();
    static void Process();

    // host only: full territory + war snapshot, sent when another player connects (see PLAYER_CONNECTED)
    static void SyncCurrentState();

    static void HandleZonesSync(Packets::Gangs::GangZonesSync* pPacket);
    static void HandleWarState(Packets::Gangs::GangWarState* pPacket);
};
