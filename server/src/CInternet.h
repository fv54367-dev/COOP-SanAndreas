#pragma once
#include <string>
#include <cstdint>
namespace coop::nat {
// ---------------------------------------------------------------- local network
uint32_t LocalIp();                     // IPv4 of the default-route interface (network order)
bool IsPrivateIp(uint32_t ipNet);       // 10/8, 172.16/12, 192.168/16, 100.64/10 (CGNAT), 127/8
std::string IpToString(uint32_t ipNet);

// ---------------------------------------------------------------- UPnP
struct UpnpResult {
    bool ok = false;
    std::string error;
    std::string externalIp;  // router WAN address, may be private when the ISP uses CGNAT
    std::string controlUrl, serviceType;
    std::string internalIp, description;
    int leaseSeconds = 0;    // Only finite leases are accepted.
};
// Blocking router discovery and SOAP; called only on the worker.
UpnpResult UpnpMapPort(uint16_t port, const std::string& description);
void UpnpUnmapPort(const UpnpResult& r, uint16_t port);

}
namespace CInternet { void Start(uint16_t port); void Stop(); }
