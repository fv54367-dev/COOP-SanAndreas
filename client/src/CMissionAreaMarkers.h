#pragma once

#include "network/packets/blips.h"

// The red cylinders of a mission, drawn on the guests exactly as the game draws them on the script authority.
// Every cylinder of a script (LOCATE_*, IS_CHAR_IN_AREA_*, angled areas, with the sphere flag) is one call of
// CTheScripts::HighlightImportantArea (0x485E00, gta-reversed RunningScript.cpp / CharacterCommands.cpp), made from 14
// command handlers. Those call sites are redirected: on the authority, a synced mission script's area is sent 5 times a
// second while it is drawn; a guest calls the same function with the same area every frame until 600 ms after the
// last packet, so the game draws it natively. Mission rules do not change (nothing is checked for the guest here).
class CMissionAreaMarkers
{
public:
    static void InjectHooks();
    // guest: draws the received areas (processScriptsEvent.after, where the scripts place theirs)
    static void Process();
    static void Handle(Packets::Blips::MissionAreaMarker* packet);
    static void Clear();
    // a received area is drawn around this point: the old checkpoint cylinder is not drawn twice
    static bool IsDrawnNear(const CVector& position);
};
