#pragma once
#include "network/packets/gps.h"
// Road route to the driver waypoint, shared with cooperative passengers (T57/T79).
// Adapted from Karimov-Akbar/sa-coop (MIT), features/waypoint/Gps.cpp; see NOTICE.md.
class CGpsRoute {
public:
    static void OnPacket(const Packets::Players::VehicleGps& packet);
    static void Process();  // search scheduling, once a frame
    static void Draw();     // before the blips of the radar: the route lies under them
};
