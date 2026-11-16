#pragma once
#include "../packet.h"
#include "../ids.h"
#include <cmath>
namespace Packets::Players {
class VehicleGps : public Packet {
    DEFINE_PACKET_TYPE(VehicleGps, ePacketType::VEHICLE_GPS, ePacketChannel::SYNC);
public:
    playerid_t driver{};
    vehicleid_t vehicle{};
    bool active = false;
    float x = 0, y = 0;
    template <typename Stream> bool Serialize(Stream& stream) {
        serialize_playerid(stream, driver);
        serialize_vehicleid(stream, vehicle);
        serialize_bool(stream, active);
        serialize_float(stream, x);
        serialize_float(stream, y);
        return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 6000 && std::abs(y) <= 6000;
    }
};
}
