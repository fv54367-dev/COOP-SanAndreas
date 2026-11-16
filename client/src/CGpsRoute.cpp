// Road route to the driver's waypoint, also shared with cooperative passengers (T57/T79).
// Adapted from Karimov-Akbar/sa-coop (MIT), features/waypoint/Gps.cpp. The route comes from the game's own car path
// finder (CPathFind::DoPathSearch) about once a second, and Draw runs where CRadar::DrawBlips starts, so it lies under
// the blips. Offsets checked against gta-reversed PathFind.h/Radar.h: ThePaths 0x96F050, m_pPathNodes +0x804,
// m_anNumNodes +0xFA4, m_anNumVehicleNodes +0x10C4, m_loadAreaRequestPending +0x3C6C, CPathNode 0x1C (pos /8 at +8,
// m_nFloodFill +0x17, water flag +0x18).
#include "stdafx.h"
#include "CGpsRoute.h"
#include "CConfigLoader.h"
#include "CMissionMode.h"
#include "SceneCompat/Game.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
namespace game = scene_game;

constexpr uintptr_t kPaths = 0x96F050;        // ThePaths
constexpr uintptr_t kMenuManager = 0xBA6748;  // CMenuManager: +0x24 m_nRadarMode, +0x59 m_bDrawingMap
constexpr uint32_t kNoNode = 0xFFFFFFFF;      // CNodeAddress: the area in the low word, the node in the high one
constexpr int kMaxNodes = 1000;
constexpr uint32_t kMapAreas = 64;            // NUM_PATH_MAP_AREAS: the vehicle nodes live there

std::vector<CVector2D> route;                 // the nodes from the player's end
uint32_t found[kMaxNodes];
CVector2D target{};
bool targetOn = false, requested = false;
uint32_t lastSearchMs = 0, radarMs = 0;
float rect[4];                                // our node request: min x, max x, min y, max y
Packets::Players::VehicleGps sharedTarget{}, sentTarget{};
uint32_t sharedMs = 0, sentMs = 0;
bool haveShared = false;
float offAtSearch = 0;                        // the player's distance from the route when it was found

// A limit adjuster that moves ThePaths moves the game's references to it as well: then there is no route rather than
// reads of the wrong memory. LOAD_PATH_NODES_IN_AREA's "mov ecx, offset ThePaths" sits at 0x491483.
bool PathsInPlace() {
    static const bool ok = [] {
        return game::Ref<uint8_t>(0x491483) == 0xB9 && game::Ref<uint32_t>(0x491484) == kPaths;
    }();
    return ok;
}

uint8_t* AreaNodes(uint32_t area) { return game::Ref<uint8_t*>(kPaths + 0x804 + area * 4); }  // m_pPathNodes

// a node of a loaded area (CPathNode, 0x1C bytes), or null
const uint8_t* Node(uint32_t a) {
    uint32_t area = a & 0xFFFF, id = a >> 16;
    if (area >= kMapAreas + 8 || !AreaNodes(area) || id >= game::Ref<uint32_t>(kPaths + 0xFA4 + area * 4)) return nullptr;
    return AreaNodes(area) + id * 0x1C;
}

CVector2D NodePos(const uint8_t* n) { return {game::Field<int16_t>(n, 0x08) / 8.0f, game::Field<int16_t>(n, 0x0A) / 8.0f}; }

float Dist(CVector2D a, CVector2D b) { return std::hypot(a.x - b.x, a.y - b.y); }

// the car node of the flood-fill class `fill` nearest to p (a waypoint has no height: the game's own pick weighs it
// three times and on a hill would take a road below)
uint32_t Nearest(CVector2D p, uint8_t fill) {
    uint32_t best = kNoNode;
    float bestD = 1e30f;
    for (uint32_t area = 0; area < kMapAreas; ++area) {
        const uint8_t* nodes = AreaNodes(area);
        uint32_t n = nodes ? game::Ref<uint32_t>(kPaths + 0x10C4 + area * 4) : 0;  // m_anNumVehicleNodes
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t* node = nodes + i * 0x1C;
            if (node[0x17] != fill || (node[0x18] & 0x80)) continue;  // m_nFloodFill, m_bWaterNode
            CVector2D q = NodePos(node);
            float d = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
            if (d < bestD) bestD = d, best = area | i << 16;
        }
    }
    return best;
}

// the route from the car to the target, else to a point halfway there, a quarter of the way...: the game's search gives
// up past 4950 nodes (a long way through a city), and a target in areas not loaded yet gives the route to the nearest
// loaded road, the next search goes on from there
void Search(const CVector& from, CVector2D to) {
    route.clear();
    uint32_t start = kNoNode, last = kNoNode;
    // CPathFind::FindNodeClosestToCoors (car nodes, any distance): here the height helps (a bridge over a road)
    game::CallMethod<0x44F460>((void*)kPaths, &start, from.x, from.y, from.z, 0, 999999.0f, 0, 0, 0, 0, 0);
    const uint8_t* s = Node(start);
    for (float t = 1.0f; s && t > 0.1f && route.size() < 2; t *= 0.5f) {
        CVector2D p{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
        uint32_t end = Nearest(p, s[0x17]);
        if (end == kNoNode || end == start) break;
        if (end == last) continue;
        last = end;
        int16_t count = 0;
        float distance = 0.0f;
        // CPathFind::DoPathSearch (car nodes): origin and its node, the pulled-in target, out nodes/count/distance, the
        // target's node, no side limit, nothing forbidden
        game::CallMethod<0x4515D0>((void*)kPaths, 0, from.x, from.y, from.z, start, p.x, p.y, 0.0f, found, &count,
            kMaxNodes, &distance, 999999.0f, &end, 999999.0f, 0, kNoNode, 0, 0);
        for (int i = 0; i < count && i < kMaxNodes; ++i) {
            const uint8_t* n = Node(found[i]);
            if (!n) break;
            route.push_back(NodePos(n));
        }
        if (route.size() < 2) route.clear();
    }
}

// the path node areas between the player and the target (up to 1.5 km ahead), loaded the way LOAD_PATH_NODES_IN_AREA
// has them loaded: the game keeps only those within 350 m of the player. Scripts use the same single request, and a
// script's is never overwritten: ours only when the game has none pending, and it lasts until we release it
bool OurRequest() {
    return requested && game::Ref<uint8_t>(kPaths + 0x3C6C) &&  // m_loadAreaRequestPending
        !std::memcmp(&game::Ref<float>(kPaths + 0x3C70), rect, sizeof rect);
}

void Request(const CVector& from, CVector2D to) {
    // a script's request replaced ours and the cycle ended: our wide rect stopped being kept when the slot
    // changed hands, so the claim is stale and there is nothing of ours left to release
    if (requested && !game::Ref<uint8_t>(kPaths + 0x3C6C)) requested = false;
    if (game::Ref<uint8_t>(kPaths + 0x3C6C) && !OurRequest()) {
        requested = false;  // a script owns the single slot: wait for it to finish, never release its request
        return;
    }
    constexpr float kAhead = 1500.0f, kMargin = 200.0f;  // the margin: the road the target lies off
    rect[0] = (std::max)((std::min)(from.x, to.x), from.x - kAhead) - kMargin;
    rect[1] = (std::min)((std::max)(from.x, to.x), from.x + kAhead) + kMargin;
    rect[2] = (std::max)((std::min)(from.y, to.y), from.y - kAhead) - kMargin;
    rect[3] = (std::min)((std::max)(from.y, to.y), from.y + kAhead) + kMargin;
    requested = true;
    game::CallMethod<0x450D70>((void*)kPaths, rect[0], rect[1], rect[2], rect[3]);  // MakeRequestForNodesToBeLoaded
}

void DropRoute() {
    if (OurRequest()) game::CallMethod<0x44DD00>((void*)kPaths);  // ReleaseRequestedNodes
    requested = targetOn = false;
    route.clear();
}

// the segment of the route the point runs along, and the point on it (the search starts the route at the node nearest
// to the car, as often behind it as not)
size_t Beside(CVector2D p, CVector2D& at) {
    for (size_t i = 0;; ++i) {
        const CVector2D& a = route[i];
        const CVector2D& b = route[i + 1];
        float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
        float t = l > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l : 1.0f;
        if (t < 1.0f || i + 2 >= route.size() || i >= 32) {
            t = (std::max)(0.0f, (std::min)(1.0f, t));
            at = {a.x + dx * t, a.y + dy * t};
            return i;
        }
    }
}

float OffRoute(CVector2D p) {
    CVector2D at;
    Beside(p, at);
    return Dist(p, at);
}

// the player's own map waypoint (the pause menu's target blip), while it is still in use
bool Target(CVector2D& out) {
    int h = FrontEndMenuManager.m_nTargetBlipIndex;
    if (!h) return false;
    uint32_t i = (uint32_t)h & 0xFFFF;
    if (i >= MAX_RADAR_TRACES) return false;
    const tRadarTrace& t = CRadar::ms_RadarTrace[i];
    if (!t.m_bInUse || t.m_nCounter != (uint16_t)((uint32_t)h >> 16)) return false;
    out = {t.m_vecPos.x, t.m_vecPos.y};
    return true;
}

CVector2D ToRadar(const CVector2D& world) {
    CVector2D r;
    CRadar::TransformRealWorldPointToRadarSpace(r, const_cast<CVector2D&>(world));
    return r;
}

// a..b of radar space inside the radar's unit circle, drawn as a band w pixels to each side; its ends inside go on by
// w, so the next band covers the notch at a bend
void Band(CVector2D a, CVector2D b, float w, const CRGBA& c) {
    float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy, k = a.x * dx + a.y * dy;
    float d = k * k - l * (a.x * a.x + a.y * a.y - 1.0f);  // |a + t (b - a)| = 1
    if (l < 1e-12f || d <= 0) return;
    float t0 = (-k - std::sqrt(d)) / l, t1 = (-k + std::sqrt(d)) / l;
    if (t1 <= 0 || t0 >= 1) return;
    CVector2D from{a.x + dx * (std::max)(t0, 0.0f), a.y + dy * (std::max)(t0, 0.0f)};
    CVector2D to{a.x + dx * (std::min)(t1, 1.0f), a.y + dy * (std::min)(t1, 1.0f)};
    CVector2D p, q;
    CRadar::TransformRadarPointToScreenSpace(p, from);
    CRadar::TransformRadarPointToScreenSpace(q, to);
    float ex = q.x - p.x, ey = q.y - p.y, len = std::sqrt(ex * ex + ey * ey);
    if (len < 0.01f) return;
    ex *= w / len;
    ey *= w / len;
    if (t0 <= 0) p.x -= ex, p.y -= ey;
    if (t1 >= 1) q.x += ex, q.y += ey;
    CSprite2d::Draw2DPolygon(p.x + ey, p.y - ex, q.x + ey, q.y - ex, p.x - ey, p.y + ex, q.x - ey, q.y + ex, c);
}
}  // namespace

void CGpsRoute::OnPacket(const Packets::Players::VehicleGps& packet) {
    if (!CConfigLoader::ms_bGpsRoute || CMissionMode::IsIndividual()) return;
    auto* me = FindPlayerPed(0);
    auto* car = FindPlayerVehicle(0, true);
    auto* net = car ? CNetworkVehicleManager::GetVehicle(car) : nullptr;
    auto* driver = CNetworkPlayerManager::GetPlayer(packet.driver);
    if (!me || !me->m_nPedFlags.bInVehicle || !net || net->m_vehicleId != packet.vehicle ||
        !driver || driver->m_bDead || driver->m_bIndividualMode || !driver->m_pPed ||
        car->m_pDriver != driver->m_pPed || car->m_pDriver == me) return;
    sharedTarget = packet;
    sharedMs = GetTickCount();
    haveShared = true;
}

void CGpsRoute::Process() {
    const uint32_t now = GetTickCount();
    if (!CConfigLoader::ms_bGpsRoute || !CNetwork::m_bConnected || !CNetwork::m_bAuthenticated) {
        haveShared = false;
        sentMs = 0;
        DropRoute();
        return;
    }
    CVector from{};
    CVector2D to{}, here{};
    bool on = false;
    auto* ped = FindPlayerPed(0);
    if (ped && !CCutsceneMgr::ms_running && !CCutsceneMgr::ms_cutsceneLoadStatus) {
        auto* car = FindPlayerVehicle(0, true);
        auto* net = car ? CNetworkVehicleManager::GetVehicle(car) : nullptr;
        const bool driving = car && car->m_pDriver == ped;
        if (driving && net && !CMissionMode::IsIndividual() && ped->m_fHealth > 0 && !ped->m_nAreaCode) {
            bool passengers = false;
            for (auto* remote : CNetworkPlayerManager::m_pPlayers) {
                if (remote->m_pPed && !remote->m_bIndividualMode && !remote->m_bDead &&
                    remote->m_pPed->m_nPedFlags.bInVehicle && remote->m_pPed->m_pVehicle == car &&
                    remote->m_pPed != car->m_pDriver) passengers = true;
            }
            if (passengers) {
                Packets::Players::VehicleGps packet{};
                CVector2D point{};
                packet.vehicle = net->m_vehicleId;
                packet.active = Target(point);
                if (packet.active) { packet.x = point.x; packet.y = point.y; }
                const bool changed = packet.vehicle != sentTarget.vehicle || packet.active != sentTarget.active ||
                    packet.x != sentTarget.x || packet.y != sentTarget.y;
                if (!sentMs || now - sentMs >= 1000 || (changed && now - sentMs >= 100)) {
                    GetPacketFactory().Send(packet);
                    sentTarget = packet;
                    sentMs = now;
                }
            } else sentMs = 0;
        } else sentMs = 0;
        auto* remoteDriver = haveShared ? CNetworkPlayerManager::GetPlayer(sharedTarget.driver) : nullptr;
        const bool sharedValid = haveShared && !CMissionMode::IsIndividual() && net &&
            net->m_vehicleId == sharedTarget.vehicle && now - sharedMs <= 2500 && remoteDriver &&
            !remoteDriver->m_bDead && !remoteDriver->m_bIndividualMode && remoteDriver->m_pPed &&
            car->m_pDriver == remoteDriver->m_pPed && ped->m_nPedFlags.bInVehicle && ped->m_fHealth > 0;
        if (!sharedValid) haveShared = false;
        bool destination = driving && Target(to);
        if (sharedValid) {
            destination = sharedTarget.active;
            to = {sharedTarget.x, sharedTarget.y};
        }
        if (car && ped->m_nPedFlags.bInVehicle) {
            switch (car->m_nVehicleSubType) {
            case VEHICLE_AUTOMOBILE:
            case VEHICLE_MTRUCK:
            case VEHICLE_QUAD:
            case VEHICLE_BIKE:
            case VEHICLE_BMX:
                break;
            default:  // no boat, plane, helicopter or train
                car = nullptr;
            }
            if (car && !CGame::currArea && !ped->m_nAreaCode && destination && PathsInPlace()) {
                from = car->GetPosition();
                here = {from.x, from.y};
                on = true;
            }
        }
    }
    if (!on || Dist(here, to) < 30.0f) {
        DropRoute();
        return;
    }
    if (now - radarMs > 1000) return;  // the radar's map was not drawn (the HUD is off): nothing to show
    // at once for a new target or when the car has left the route, else once a second
    const bool urgent = !targetOn || Dist(to, target) > 5.0f || (route.size() >= 2 && OffRoute(here) > offAtSearch + 40.0f);
    if (now - lastSearchMs < (urgent ? 250u : 1000u)) return;
    lastSearchMs = now;
    target = to;
    targetOn = true;
    Request(from, to);
    Search(from, to);
    offAtSearch = route.size() >= 2 ? OffRoute(here) : 0.0f;
}

void CGpsRoute::Draw() {
    // only where CRadar::DrawMap draws the radar's map: not the pause-menu map (m_bDrawingMap), not the radar of blips
    // only (m_nRadarMode 1), not an interior
    if (!CConfigLoader::ms_bGpsRoute || CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneLoadStatus || game::Ref<uint8_t>(kMenuManager + 0x59) ||
        game::Ref<int32_t>(kMenuManager + 0x24) == 1 || CGame::currArea)
        return;
    radarMs = GetTickCount();
    if (route.size() < 2) return;
    CVector2D at;
    size_t i = Beside(CRadar::vec2DRadarOrigin, at);  // the point of the map the radar centres on
    const float w = (float)RsGlobal.maximumHeight / 448.0f * 1.2f;  // 5-6 pixels across at 1080p
    const CRGBA colour(225, 45, 45, 255);
    for (CVector2D a = ToRadar(at); i + 1 < route.size(); ++i) {
        CVector2D b = ToRadar(route[i + 1]);
        Band(a, b, w, colour);
        a = b;
    }
}
