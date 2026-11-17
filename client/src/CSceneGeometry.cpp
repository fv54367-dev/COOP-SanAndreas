// Adapted from Karimov-Akbar/sa-coop (MIT), cutscene-partner; see NOTICE.md.
#include "SceneCompat/SceneGeometry.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace coop::scene::geometry {
using namespace scene_game;
namespace {
constexpr uintptr_t kCutsceneObjects = 0xBC3F18, kNumCutsceneObjects = 0xBC3FE4;
constexpr uintptr_t kCurrArea = 0xB72914;
}

// ---- world queries (CWorld): collision
bool Seen(const scene_game::CVector& a, const scene_game::CVector& b) {  // nothing solid in between (see-through surfaces do not count)
    return scene_game::Call<0x56A490, bool>(&a, &b, true, true, false, true, false, true, false);
}
bool Floor(float x, float y, float fromZ, float& z) {  // the first surface below (x, y, fromZ)
    bool found = false;
    z = scene_game::Call<0x5696C0, float>(x, y, fromZ, &found, (scene_game::CEntity**)nullptr);
    return found;
}
bool Free(const scene_game::CVector& c, float r) {  // no building, vehicle or object there
    return !scene_game::Call<0x569E20, scene_game::CEntity*>(c, r, (scene_game::CEntity*)nullptr, true, true, false, true, false, false);
}

scene_game::CVector Flat(scene_game::CVector v) {
    v.z = 0;
    float l = v.Length2D();
    return l > 1e-4f ? v * (1.0f / l) : scene_game::CVector{0, 1, 0};
}
float HeadingOf(const scene_game::CVector& dir) { return std::atan2(-dir.x, dir.y); }
scene_game::CVector Forward(float heading) { return {-std::sin(heading), std::cos(heading), 0}; }
scene_game::CVector Turn(const scene_game::CVector& v, float a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), 0}; }
scene_game::CVector Transform(scene_game::CEntity* e, const scene_game::CVector& v, bool point) {  // model space -> world
    if (scene_game::CMatrix* m = e->Matrix()) {
        scene_game::CVector r = m->right * v.x + m->forward * v.y + m->up * v.z;
        return point ? r + m->pos : r;
    }
    float h = e->Placement().heading, c = std::cos(h), s = std::sin(h);
    scene_game::CVector r{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
    return point ? r + e->Placement().pos : r;
}

scene_game::CEntity* CutsceneObject(int i) { return scene_game::Ref<scene_game::CEntity*>(kCutsceneObjects + (uintptr_t)i * 4); }
int NumCutsceneObjects() { return std::clamp(scene_game::Ref<int32_t>(kNumCutsceneObjects), 0, 50); }
scene_game::CEntity* CutsceneCj() {
    for (int i = 0; i < NumCutsceneObjects(); ++i)
        if (scene_game::CEntity* o = CutsceneObject(i); o && o->ModelIndex() == 1) return o;  // "csplay"
    return nullptr;
}
bool IsPerson(scene_game::CEntity* e) {  // the people of a cutscene are skinned; its props (furniture, doors) are not
    auto* rw = (uint8_t*)e->RwObject();
    return rw && rw[0] == 2 && scene_game::Call<0x734A40, void*>(rw);  // RpClump with GetAnimHierarchyFromSkinClump
}

// ---- drawn triangles (render geometry) of an entity, in world coordinates
struct Visit {
    TriangleFn fn;
    void* user;
};
void* __cdecl VisitAtomic(void* atomic, void* data) {
    auto* v = (Visit*)data;
    auto* geom = scene_game::Field<uint8_t*>(atomic, 0x18);  // RpAtomic::geometry
    auto* frame = scene_game::Field<void*>(atomic, 0x04);    // RwObject::parent
    if (!geom || !frame || !(scene_game::Field<uint8_t>(atomic, 0x02) & 4)) return atomic;  // rpATOMICRENDER
    int tris = scene_game::Field<int32_t>(geom, 0x10), verts = scene_game::Field<int32_t>(geom, 0x14);
    auto* tr = scene_game::Field<uint16_t*>(geom, 0x2C);    // RpTriangle: three vertex indices and the material
    auto* morph = scene_game::Field<uint8_t*>(geom, 0x5C);  // RpMorphTarget[0]
    auto* vx = morph ? scene_game::Field<scene_game::CVector*>(morph, 0x14) : nullptr;
    auto* m = scene_game::Call<0x7F0990, scene_game::CMatrix*>(frame);  // RwFrameGetLTM
    if (!tr || !vx || !m || tris <= 0 || verts <= 0) return atomic;
    for (int k = 0; k < tris; ++k) {
        const uint16_t* t = tr + k * 4;
        if (t[0] >= verts || t[1] >= verts || t[2] >= verts) continue;
        scene_game::CVector w[3];
        for (int j = 0; j < 3; ++j) {
            const scene_game::CVector& p = vx[t[j]];
            w[j] = m->right * p.x + m->forward * p.y + m->up * p.z + m->pos;
        }
        v->fn(w[0], w[1], w[2], v->user);
    }
    return atomic;
}
void ForEachTriangle(scene_game::CEntity* e, TriangleFn fn, void* user) {
    auto* rw = (uint8_t*)e->RwObject();
    if (!rw) return;
    Visit v{fn, user};
    if (rw[0] == 1) VisitAtomic(rw, &v);                              // rpATOMIC
    else if (rw[0] == 2) scene_game::Call<0x749B70>(rw, &VisitAtomic, (void*)&v);  // RpClumpForAllAtomics
}

// The cutscene's cars are props of their own: big (the cube the cutscene keeps around each of its objects,
// CCutsceneMgr::UpdateCutsceneObjectBoundingBox, is several metres) and not skinned like its people. Their footprint
// comes from what is drawn, relative to the object's position.
void Extend(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c, void* user) {
    auto* r = (Bounds*)user;
    for (const scene_game::CVector* v : {&a, &b, &c})
        r->x0 = std::min(r->x0, v->x), r->x1 = std::max(r->x1, v->x), r->y0 = std::min(r->y0, v->y), r->y1 = std::max(r->y1, v->y);
}
bool IsCar(scene_game::CEntity* e, Bounds& b) {
    void* mi = scene_game::Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)e->ModelIndex() * 4);
    auto* col = mi ? scene_game::Field<uint8_t*>(mi, 0x14) : nullptr;  // CColModel: its box first
    if (!col || IsPerson(e)) return false;
    scene_game::CVector lo = scene_game::Field<scene_game::CVector>(col, 0x00), hi = scene_game::Field<scene_game::CVector>(col, 0x0C);
    if (std::max(hi.x - lo.x, hi.y - lo.y) < 4.0f) return false;
    ForEachTriangle(e, &Extend, &b);
    scene_game::CVector p = e->GetPosition();
    b.x0 -= p.x, b.x1 -= p.x, b.y0 -= p.y, b.y1 -= p.y;
    return std::max(b.x1 - b.x0, b.y1 - b.y0) >= 2.5f;
}

// ---- what is drawn around CJ, as a height map
// Cutscene sets often have walls and furniture without collision, and the cutscene's own props (tables, chairs,
// sofas) have none at all, so the drawn triangles are what counts. Per 10 cm cell, above CJ's floor: the highest
// thing a standing person bumps into, the highest flat surface at sitting height, and whether a floor is drawn.
Grid g_grid;

float SegDist2(float px, float py, const scene_game::CVector& a, const scene_game::CVector& b) {
    float dx = b.x - a.x, dy = b.y - a.y, l = dx * dx + dy * dy;
    float t = l > 1e-8f ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / l, 0.0f, 1.0f) : 0.0f;
    float ex = a.x + dx * t - px, ey = a.y + dy * t - py;
    return ex * ex + ey * ey;
}

void Grid::Add(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c) {
    float za = a.z - floor, zb = b.z - floor, zc = c.z - floor;
    float zmin = std::min({za, zb, zc}), zmax = std::max({za, zb, zc});
    if (zmax < -0.15f || zmin > kBumpHigh) return;
    scene_game::CVector n = (b - a).Cross(c - a);
    float len = n.Length();
    if (len < 1e-6f) return;
    bool flat = std::fabs(n.z) / len > 0.7f;
    if (!flat && zmax < kBumpLow) return;  // a kerb or a step: the feet go over it
    int cx0 = std::max(0, (int)std::floor((std::min({a.x, b.x, c.x}) - x0) / kCell));
    int cx1 = std::min(kCells - 1, (int)std::floor((std::max({a.x, b.x, c.x}) - x0) / kCell));
    int cy0 = std::max(0, (int)std::floor((std::min({a.y, b.y, c.y}) - y0) / kCell));
    int cy1 = std::min(kCells - 1, (int)std::floor((std::max({a.y, b.y, c.y}) - y0) / kCell));
    float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    bool edgeOn = std::fabs(d) < 1e-6f;  // a wall seen from above
    uint8_t wall = (uint8_t)(std::min(zmax, 2.5f) / 0.02f);
    // the drawn side of a wall: film sets put cameras behind walls that are only drawn from inside the room
    uint8_t side = 0xFF;
    if (!flat && n.Length2D() > 1e-6f)
        side = (uint8_t)(1u << (((int)std::floor(std::atan2(n.y, n.x) / 0.7853982f + 0.5f) + 8) % 8));
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            float px = x0 + (cx + 0.5f) * kCell, py = y0 + (cy + 0.5f) * kCell;
            float l1 = 0, l2 = 0, l3 = 0;
            bool inside = false;
            if (!edgeOn) {
                l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / d;
                l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / d;
                l3 = 1 - l1 - l2;
                inside = l1 >= 0 && l2 >= 0 && l3 >= 0;
            }
            int i = cy * kCells + cx;
            if (flat) {
                if (!inside) continue;
                float z = l1 * za + l2 * zb + l3 * zc;
                if (z > -0.15f && z < 0.12f) {
                    ground[i] = 1;
                } else if (z >= kBumpLow && z <= kBumpHigh) {
                    top[i] = std::max(top[i], (uint8_t)(z / 0.02f));
                    faces[i] = 0xFF;
                    if (z >= 0.3f && z <= 0.7f) seat[i] = std::max(seat[i], (uint8_t)(z * 100));
                }
            } else {  // walls and the sides of furniture: every cell they pass, as high as they reach
                constexpr float kNear = kCell * kCell * 0.36f;
                if (!inside && SegDist2(px, py, a, b) > kNear && SegDist2(px, py, b, c) > kNear && SegDist2(px, py, c, a) > kNear)
                    continue;
                top[i] = std::max(top[i], wall);
                faces[i] |= side;
            }
        }
}

void AddTriangle(const scene_game::CVector& a, const scene_game::CVector& b, const scene_game::CVector& c, void* grid) { ((Grid*)grid)->Add(a, b, c); }

// Everything drawn in this interior within 12 m of CJ: buildings and objects (no LODs), and the cutscene's props.
void BuildGrid(const scene_game::CVector& cj, float floor) {
    Grid& g = g_grid;
    memset(g.top, 0, sizeof g.top);
    memset(g.seat, 0, sizeof g.seat);
    memset(g.ground, 0, sizeof g.ground);
    memset(g.faces, 0, sizeof g.faces);
    g.floor = floor;
    g.x0 = cj.x - kCells * kCell * 0.5f;
    g.y0 = cj.y - kCells * kCell * 0.5f;
    int area = scene_game::Ref<int32_t>(kCurrArea);
    static scene_game::CEntity* ents[1024];
    int16_t n = 0;
    scene_game::Call<0x564A20>(&cj, 45.0f, false, &n, (int16_t)1024, ents, true, false, false, true, false);  // FindObjectsInRange
    scene_game::CEntity* own[50];  // the cutscene's objects are world objects too: its people must stay out
    int owned = NumCutsceneObjects();
    for (int i = 0; i < owned; ++i) own[i] = CutsceneObject(i);
    for (int i = 0; i < n; ++i) {
        scene_game::CEntity* e = ents[i];
        if (!(e->Flags() & 0x80) || scene_game::Field<uint8_t>(e, 0x34)) continue;  // not drawn, or the LOD of others
        if (std::find(own, own + owned, e) != own + owned) continue;
        if (e->AreaCode() != area && e->AreaCode() != 13) continue;     // another interior
        void* mi = scene_game::Ref<void*>(addr::ModelInfoPtrs + (uintptr_t)e->ModelIndex() * 4);
        void* col = mi ? scene_game::Field<void*>(mi, 0x14) : nullptr;  // its bounds: CBaseModelInfo::m_pColModel
        if (col) {
            scene_game::CVector centre = Transform(e, scene_game::Field<scene_game::CVector>(col, 0x18), true);
            if ((centre - cj).Length2D() - scene_game::Field<float>(col, 0x24) > 17.0f) continue;
        } else if ((e->GetPosition() - cj).Length2D() > 30.0f) {
            continue;
        }
        ForEachTriangle(e, &AddTriangle, &g);
    }
    for (int i = 0; i < NumCutsceneObjects(); ++i)
        if (scene_game::CEntity* o = CutsceneObject(i); o && o->ModelIndex() != 1 && !IsPerson(o)) ForEachTriangle(o, &AddTriangle, &g);
}

// nothing drawn between the camera and the point (the last 35 cm belong to the spot: its seat, its wall);
// a wall counts only when its drawn side faces the camera
bool GridSeen(const scene_game::CVector& from, const scene_game::CVector& to) {
    scene_game::CVector d = to - from;
    float len = d.Length2D();
    int steps = (int)(len / 0.08f);
    uint8_t facing = 0;  // the directions a side must face to be seen from the camera
    for (int s = 0; s < 8; ++s)
        if (std::cos(s * 0.7853982f) * -d.x + std::sin(s * 0.7853982f) * -d.y > 0) facing |= (uint8_t)(1u << s);
    for (int k = 1; k < steps; ++k) {
        float t = (float)k / (float)steps;
        if ((1 - t) * len < 0.35f) break;
        scene_game::CVector p = from + d * t;
        int i;
        if (!g_grid.Index(p.x, p.y, i) || !g_grid.top[i]) continue;
        if (g_grid.floor + g_grid.top[i] * 0.02f >= p.z && (g_grid.faces[i] & facing)) return false;
    }
    return true;
}

// The floor one walks to from CJ: a flood fill of the obstacle map, kept 20 cm off what is drawn (no squeezing through
// a gap a person does not fit). The last try took a place behind Zero's counter, by the till (31.10). The last chance
// squeezes through gaps of 30 cm (margin 1).
uint8_t g_reach[kCells * kCells];
void BuildReach(const scene_game::CVector& from, int margin) {
    static uint8_t wall[kCells * kCells];
    memset(wall, 0, sizeof wall);
    memset(g_reach, 0, sizeof g_reach);
    for (int y = 0; y < kCells; ++y)
        for (int x = 0; x < kCells; ++x)
            if (g_grid.top[y * kCells + x])
                for (int yy = std::max(0, y - margin); yy <= std::min(kCells - 1, y + margin); ++yy)
                    memset(wall + yy * kCells + std::max(0, x - margin), 1, (size_t)(std::min(kCells - 1, x + margin) - std::max(0, x - margin) + 1));
    int start = -1;  // CJ's cell, or the nearest open one: he may stand right at a counter
    for (float r = 0; r <= 1.5f && start < 0; r += kCell)
        for (int k = 0; k < 16 && start < 0; ++k) {
            int i;
            if (g_grid.Index(from.x + std::cos(k * 0.3927f) * r, from.y + std::sin(k * 0.3927f) * r, i) && !wall[i]) start = i;
        }
    // CJ hemmed in by furniture (on a sofa, at the table of a talk): no walk to judge by, rather than every place of the
    // last try refused
    if (start < 0) {
        memset(g_reach, 1, sizeof g_reach);
        return;
    }
    std::vector<int> open{start};
    g_reach[start] = 1;
    for (size_t q = 0; q < open.size(); ++q) {
        int i = open[q], x = i % kCells, y = i / kCells;
        for (int n : {x > 0 ? i - 1 : -1, x < kCells - 1 ? i + 1 : -1, y > 0 ? i - kCells : -1, y < kCells - 1 ? i + kCells : -1})
            if (n >= 0 && !g_reach[n] && !wall[n]) {
                g_reach[n] = 1;
                open.push_back(n);
            }
    }
}
bool Reachable(const scene_game::CVector& p) {
    int i;
    return g_grid.Index(p.x, p.y, i) && g_reach[i];
}

}  // namespace coop::scene::geometry
