#pragma once

#include "stdafx.h"

// Syncs the WORLD subset of keyboard cheats (CCheat::m_aCheatsActive) between players.
// Player-only cheats (health, armour, weapons, money, wanted level, vehicle spawns, stats...) stay local.
class CCheatSync
{
public:
    static void Process();
    static void SyncCurrentState();
    static void HandlePacket(Packets::World::CheatSync* pCheatSync);
    static void HandleAnnounce(Packets::World::CheatAnnounce* pAnnounce);
};
