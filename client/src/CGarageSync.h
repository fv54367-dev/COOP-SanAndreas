#pragma once
#include "stdafx.h"

// T90: opening/closing of script-driven garage doors (mission garages, lockups) shared between players.
// Adapted from Karimov-Akbar/sa-coop (MIT), features/garages/Garages.cpp (door states, script door types); see NOTICE.md.
class CGarageSync
{
public:
    static void Process();  // once a frame: detect local door changes, apply pending remote ones
    static void HandlePacket(const Packets::World::GarageDoor& packet);
};
