#include "stdafx.h"
#include "CInternet.h"
#include "ConnectionCode.h"
#include <ws2tcpip.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
namespace coop::nat {
static std::string Trim(std::string s) {
    auto a=s.find_first_not_of(" \r\n\t");
    if(a==std::string::npos) return "";
    return s.substr(a,s.find_last_not_of(" \r\n\t")-a+1);
}
// ============================================================================ helpers
std::string IpToString(uint32_t ipNet) {
    const uint8_t* b = (const uint8_t*)&ipNet;
    return std::to_string(b[0])+"."+std::to_string(b[1])+"."+std::to_string(b[2])+"."+std::to_string(b[3]);
}

static void EnsureWinsock() {
    static bool done = false;
    if (!done) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        done = true;
    }
}

uint32_t LocalIp() {
    EnsureWinsock();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    uint32_t ip = 0;
    // connect() on UDP sends nothing, it only selects the outgoing interface
    if (connect(s, (sockaddr*)&to, sizeof(to)) == 0) {
        sockaddr_in me{};
        int len = sizeof(me);
        if (getsockname(s, (sockaddr*)&me, &len) == 0) ip = me.sin_addr.s_addr;
    }
    closesocket(s);
    return ip;
}

bool IsPrivateIp(uint32_t ipNet) {
    uint32_t ip = ntohl(ipNet);
    if ((ip >> 24) == 0 || (ip >> 24) >= 224 || (ip >> 16) == 0xA9FE) return true;
    return (ip >> 24) == 10 || (ip >> 20) == (172 << 4 | 1) || (ip >> 16) == (192 << 8 | 168) ||
           (ip >> 22) == ((100 << 2) | 1) /* 100.64/10 */ || (ip >> 24) == 127 || ip == 0;
}

static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Extracts the text of the first <tag>...</tag> after `from`.
static std::string XmlTag(const std::string& xml, const std::string& tag, size_t from = 0, size_t* endPos = nullptr) {
    std::string open = "<" + tag + ">", close = "</" + tag + ">";
    // tags may carry namespace prefixes, so search case-insensitively for the local name
    std::string lx = Lower(xml), lo = Lower(open), lc = Lower(close);
    size_t a = lx.find(lo, from);
    if (a == std::string::npos) {
        // try any prefix: ":tag>"
        size_t p = lx.find(":" + Lower(tag) + ">", from);
        if (p == std::string::npos) return "";
        a = lx.rfind('<', p);
        if (a == std::string::npos) return "";
        lo = lx.substr(a, p + tag.size() + 2 - a);
        lc = "</" + lo.substr(1);
    }
    size_t b = lx.find(lc, a + lo.size());
    if (b == std::string::npos) return "";
    if (endPos) *endPos = b + lc.size();
    return Trim(xml.substr(a + lo.size(), b - a - lo.size()));
}

// ============================================================================ minimal HTTP over TCP (for the router)
struct Url {
    std::string host, path = "/";
    uint16_t port = 80;
};

static bool ParseUrl(const std::string& url, Url& u) {
    std::string s = url;
    size_t p = s.find("://");
    if (p != std::string::npos) { if (s.substr(0,p) != "http") return false; s = s.substr(p + 3); }
    size_t slash = s.find('/');
    std::string hostport = slash == std::string::npos ? s : s.substr(0, slash);
    u.path = slash == std::string::npos ? "/" : s.substr(slash);
    size_t colon = hostport.find(':');
    u.host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
    u.port = colon == std::string::npos ? 80 : (uint16_t)atoi(hostport.c_str() + colon + 1);
    return !u.host.empty();
}

static std::string RawHttp(const Url& u, const std::string& request, int timeoutMs) {
    EnsureWinsock();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) return "";
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DWORD to = (DWORD)timeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
    std::string out;
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    int connected = connect(s, res->ai_addr, (int)res->ai_addrlen);
    fd_set writable; FD_ZERO(&writable); FD_SET(s, &writable);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (connected == 0 || (WSAGetLastError() == WSAEWOULDBLOCK && select(0, nullptr, &writable, nullptr, &timeout) > 0)) {
        int error = 0, errorSize = sizeof(error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&error, &errorSize);
        if (error) { closesocket(s); freeaddrinfo(res); return ""; }
        nonblocking = 0; ioctlsocket(s, FIONBIO, &nonblocking);
        size_t sent = 0;
        while (sent < request.size()) {
            int n = send(s, request.data()+sent, (int)(request.size()-sent), 0);
            if (n <= 0) { closesocket(s); freeaddrinfo(res); return ""; }
            sent += n;
        }
        ULONGLONG deadline = GetTickCount64() + timeoutMs;
        char buf[4096];
        int n;
        while (GetTickCount64() < deadline && (n = recv(s, buf, sizeof(buf), 0)) > 0) {
            out.append(buf, n);
            if (out.size() > 512 * 1024) break;
        }
    }
    closesocket(s);
    freeaddrinfo(res);
    return out;
}

static std::string HttpBody(const std::string& resp, int* status = nullptr) {
    if (status) *status = resp.size() > 12 ? atoi(resp.c_str() + 9) : 0;
    size_t p = resp.find("\r\n\r\n");
    if (p == std::string::npos) return "";
    std::string headers = Lower(resp.substr(0, p));
    std::string body = resp.substr(p + 4);
    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string out;
        size_t i = 0;
        while (i < body.size()) {
            size_t e = body.find("\r\n", i);
            if (e == std::string::npos) break;
            size_t len = strtoul(body.substr(i, e - i).c_str(), nullptr, 16);
            if (len == 0 || len > body.size() - (e + 2)) break;  // a length past the end wrapped i in 32 bits
            out += body.substr(e + 2, len);
            i = e + 2 + len + 2;
        }
        return out;
    }
    return body;
}

// ============================================================================ UPnP IGD
static std::vector<std::string> SsdpDiscover(int timeoutMs) {
    EnsureWinsock();
    std::vector<std::string> locations;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return locations;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = LocalIp();  // send from the interface that has the default route
    bind(s, (sockaddr*)&local, sizeof(local));
    int ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    sockaddr_in mc{};
    mc.sin_family = AF_INET;
    mc.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &mc.sin_addr);
    const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                             "urn:schemas-upnp-org:service:WANIPConnection:1",
                             "urn:schemas-upnp-org:service:WANPPPConnection:1",
                             "urn:schemas-upnp-org:device:InternetGatewayDevice:2"};
    for (const char* st : targets) {
        std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nST: ") + st +
                          "\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n\r\n";
        sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&mc, sizeof(mc));
    }
    uint64_t end = GetTickCount64() + (uint64_t)timeoutMs;
    char buf[2048];
    while (GetTickCount64() < end) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s, &fds);
        timeval tv{0, 100 * 1000};
        if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) continue;
        buf[n] = 0;
        std::string resp(buf), low = Lower(resp);
        size_t p = low.find("\r\nlocation:");
        if (p == std::string::npos) continue;
        size_t e = resp.find("\r\n", p + 2);
        std::string loc = Trim(resp.substr(p + 11, e - p - 11));
        if (std::find(locations.begin(), locations.end(), loc) == locations.end()) locations.push_back(loc);
        if (locations.size() >= 8) break;
    }
    closesocket(s);
    return locations;
}

static std::string Soap(const UpnpResult& r, const std::string& action, const std::string& args, int* status) {
    Url u;
    if (!ParseUrl(r.controlUrl, u)) { if (status) *status = 0; return ""; }
    std::string body = "<?xml version=\"1.0\"?>\r\n"
                       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                       "<u:" + action + " xmlns:u=\"" + r.serviceType + "\">" + args + "</u:" + action +
                       "></s:Body></s:Envelope>\r\n";
    std::string req = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                      "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + r.serviceType + "#" + action +
                      "\"\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    return HttpBody(RawHttp(u, req, 4000), status);
}

UpnpResult UpnpMapPort(uint16_t port, const std::string& description) {
    UpnpResult r;
    uint32_t local = LocalIp();
    if (!local) {
        r.error = "no network interface";
        return r;
    }
    auto locations = SsdpDiscover(2500);
    if (locations.empty()) {
        r.error = "UPnP router not found";
        return r;
    }
    for (auto& loc : locations) {
        Url u;
        if (!ParseUrl(loc, u)) continue;
        std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) +
                          "\r\nConnection: close\r\n\r\n";
        std::string xml = HttpBody(RawHttp(u, req, 3000));
        if (xml.empty()) continue;
        std::string base = XmlTag(xml, "URLBase");
        if (base.empty()) base = "http://" + u.host + ":" + std::to_string(u.port);
        while (!base.empty() && base.back() == '/') base.pop_back();
        // find WANIPConnection / WANPPPConnection service blocks
        size_t pos = 0;
        while (true) {
            size_t end = 0;
            std::string svc = XmlTag(xml, "service", pos, &end);
            if (svc.empty()) break;
            pos = end;
            std::string type = XmlTag(svc, "serviceType");
            if (type.find("WANIPConnection") == std::string::npos && type.find("WANPPPConnection") == std::string::npos)
                continue;
            std::string ctl = XmlTag(svc, "controlURL");
            if (ctl.empty()) continue;
            r.serviceType = type;
            r.controlUrl = ctl.rfind("http", 0) == 0 ? ctl : base + (ctl[0] == '/' ? "" : "/") + ctl;
            int st = 0;
            std::string ipResp = Soap(r, "GetExternalIPAddress", "", &st);
            r.externalIp = XmlTag(ipResp, "NewExternalIPAddress");
            std::string localStr = IpToString(local);
            r.internalIp = localStr; r.description = description;
            std::string existing = Soap(r, "GetSpecificPortMappingEntry", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", &st);
            if (st != 500 || XmlTag(existing, "errorCode") != "714") { r.error = "UDP port already mapped or lookup unsupported"; continue; }
            // a finite lease first: a crashed server must not leave the port open for good
            for (int lease : {3600}) {
                std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
                                   "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" +
                                   std::to_string(port) + "</NewInternalPort><NewInternalClient>" + localStr +
                                   "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" +
                                   description + "</NewPortMappingDescription><NewLeaseDuration>" +
                                   std::to_string(lease) + "</NewLeaseDuration>";
                Soap(r, "AddPortMapping", args, &st);
                if (st == 200) {
                    r.ok = true;
                    r.leaseSeconds = lease;
                    return r;
                }
            }
            r.error = "router rejected finite lease, HTTP " + std::to_string(st);
        }
    }
    if (r.error.empty()) r.error = "router has no supported WANIPConnection";
    return r;
}

static bool OwnsMapping(const UpnpResult& r, uint16_t port) {
    int status = 0;
    std::string body = Soap(r, "GetSpecificPortMappingEntry", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", &status);
    return status == 200 && XmlTag(body, "NewInternalClient") == r.internalIp &&
        XmlTag(body, "NewInternalPort") == std::to_string(port) && XmlTag(body, "NewPortMappingDescription") == r.description;
}

void UpnpUnmapPort(const UpnpResult& r, uint16_t port) {
    if (!r.ok || !OwnsMapping(r, port)) return;
    int st = 0;
    Soap(r, "DeletePortMapping",
         "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) +
             "</NewExternalPort><NewProtocol>UDP</NewProtocol>",
         &st);
    CServerLog::Printf("[UPnP] UDP %u cleanup HTTP %d (lease also expires).\n", port, st);
}


bool Renew(const UpnpResult& r, uint16_t port) {
    if (!OwnsMapping(r, port)) return false;
    int status = 0;
    Soap(r, "AddPortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>" + std::to_string(port) + "</NewInternalPort><NewInternalClient>" + r.internalIp + "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" + r.description + "</NewPortMappingDescription><NewLeaseDuration>3600</NewLeaseDuration>", &status);
    return status == 200;
}
}
namespace CInternet {
static coop::nat::UpnpResult mapping;
static uint16_t mappedPort;
static std::mutex mutex;
static std::condition_variable wake;
static bool stopping = false;
static std::thread worker;
void Start(uint16_t port) {
    if (!CConfigManager::GetUpnp() || CConfigManager::GetConfigTestCommands() || port == 6799 || port == 6800) {
        CServerLog::Printf("[UPnP] Disabled (config/local test). VPN/IP available.\n"); return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (worker.joinable()) return;
    stopping = false;
    mapping = {};
    mappedPort = port;
    worker = std::thread([port] {
        try {
            auto result = coop::nat::UpnpMapPort(port, "COOPSanAndreas-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
            std::unique_lock<std::mutex> lock(mutex);
            mapping = result;
            if (!mapping.ok) { CServerLog::Printf("[UPnP] %s. Use VPN/IP or manual forwarding.\n", mapping.error.c_str()); return; }
            ENetAddress ep{};
            if (enet_address_set_host_ip(&ep, mapping.externalIp.c_str()) == 0 && !coop::nat::IsPrivateIp(ep.host))
                CServerLog::Printf("[UPnP] UDP %u open (lease 3600s). Connection code: %s\n", port, ConnectionCode::Encode(ep.host, port).c_str());
            else CServerLog::Printf("[UPnP] UDP mapped but WAN IP is private/invalid (CGNAT/double NAT). Use VPN/IP.\n");
            while (!wake.wait_for(lock, std::chrono::minutes(30), [] { return stopping; })) {
                auto lease = mapping;
                lock.unlock();
                const bool renewed = coop::nat::Renew(lease, port);
                lock.lock();
                if (!renewed) CServerLog::Printf("[UPnP] Lease renewal failed; expires within 3600s. Use VPN/IP.\n");
            }
        } catch (const std::exception& e) {
            CServerLog::Printf("[UPnP] Worker failed: %s. Use VPN/IP.\n", e.what());
        } catch (...) {
            CServerLog::Printf("[UPnP] Worker failed. Use VPN/IP.\n");
        }
    });
}
void Stop() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
    wake.notify_all();
    if (worker.joinable()) worker.join();
    if (mapping.ok) { coop::nat::UpnpUnmapPort(mapping, mappedPort); mapping.ok = false; }
}
}
